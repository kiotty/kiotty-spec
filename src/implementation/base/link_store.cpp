#include "implementation/base/link_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <algorithm>
#include <deque>
#include <map>
#include <regex>
#include <set>

namespace kiotty::base {

using core::ItemRecord;
using core::LinkRecord;

namespace {

constexpr std::string_view kSelect =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0 ";

LinkRecord readLink(const core::Statement &stmt) {
    LinkRecord record;
    record.fromId = stmt.text(0);
    record.toId = stmt.text(1);
    core::parse(stmt.text(2), record.type);
    record.cpSeq = stmt.integer(3);
    return record;
}

// 링크 끝점의 종류. 항목이면 kind/category, 파일이면 role.
struct Node {
    bool isFile = false;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category{};
    KiottySpecFileRole role{};
};

bool isFunctional(const Node &n) {
    return !n.isFile && n.kind == KiottySpecItemKind_Requirement && n.category == KiottySpecItemCategory_ReqFunctional;
}
bool isItem(const Node &n, KiottySpecItemKind kind) { return !n.isFile && n.kind == kind; }
bool isModule(const Node &n) { return isItem(n, KiottySpecItemKind_Design) && n.category == KiottySpecItemCategory_DsnModule; }

bool allowed(KiottySpecLinkType type, const Node &from, const Node &to) {
    switch (type) {
    case KiottySpecLinkType_Satisfies:
        return (isItem(from, KiottySpecItemKind_Interface) && isFunctional(to)) ||
               (isItem(from, KiottySpecItemKind_Design) &&
                (isItem(to, KiottySpecItemKind_Requirement) || isItem(to, KiottySpecItemKind_Interface)));
    case KiottySpecLinkType_Verifies:
        return (isItem(from, KiottySpecItemKind_UnitTest) && isItem(to, KiottySpecItemKind_Design)) ||
               (isItem(from, KiottySpecItemKind_SystemTest) && isFunctional(to)) ||
               (isItem(from, KiottySpecItemKind_E2eTest) && isItem(to, KiottySpecItemKind_Interface));
    case KiottySpecLinkType_Implements:
        return (from.isFile && (from.role == KiottySpecFileRole_GuiDraft || from.role == KiottySpecFileRole_CHeader) &&
                isItem(to, KiottySpecItemKind_Interface)) ||
               (from.isFile && from.role == KiottySpecFileRole_Source && isItem(to, KiottySpecItemKind_Design));
    case KiottySpecLinkType_Covers:
        return isItem(from, KiottySpecItemKind_Milestone) &&
               (isItem(to, KiottySpecItemKind_Design) || isItem(to, KiottySpecItemKind_UnitTest));
    case KiottySpecLinkType_DependsOn:
        return isModule(from) && isModule(to);
    }
    return false;
}

constexpr std::string_view kAllowedHelp =
    "허용되는 링크: interface satisfies requirement(functional) | design satisfies requirement/interface | "
    "unit_test verifies design | system_test verifies requirement(functional) | e2e_test verifies interface | "
    "file(gui_draft/c_header) implements interface | file(source) implements design | "
    "milestone covers design/unit_test | design(module) depends_on design(module)";

} // namespace

void LinkStore::validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    core::require(fromId != toId, KiottySpecStatus_InvalidArg, "자기 자신으로의 링크는 만들 수 없음");
    const auto resolve = [&](std::string_view id) {
        Node node;
        if (const auto item = items_.find(id)) {
            node.kind = item->kind;
            node.category = item->category;
            return node;
        }
        if (const auto file = files_.find(id)) {
            node.isFile = true;
            node.role = file->role;
            return node;
        }
        core::fail(KiottySpecStatus_NotFound, "항목·파일 없음: " + std::string(id));
    };
    const Node from = resolve(fromId);
    const Node to = resolve(toId);
    core::require(allowed(type, from, to), KiottySpecStatus_InvalidArg,
                  std::string(fromId) + " " + std::string(core::name(type)) + " " + std::string(toId) +
                      " 는 허용되지 않음. " + std::string(kAllowedHelp));
}

void LinkStore::add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    validate(fromId, toId, type);
    if (!exists(fromId, toId, type)) {
        writeVersion(fromId, toId, type, false);
    }
}

void LinkStore::remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    core::require(exists(fromId, toId, type), KiottySpecStatus_NotFound,
                  "링크 없음: " + std::string(fromId) + " " + std::string(core::name(type)) + " " + std::string(toId));
    writeVersion(fromId, toId, type, true);
}

void LinkStore::removeAllFor(std::string_view id) {
    for (const auto &link : list(id, KiottySpecDirection_Both, static_cast<KiottySpecLinkType>(0))) {
        writeVersion(link.fromId, link.toId, link.type, true);
    }
}

bool LinkStore::exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "AND v.from_id = ?2 AND v.to_id = ?3 AND v.type = ?4");
    stmt.bindAll(cut, fromId, toId, core::name(type));
    return stmt.step();
}

std::vector<LinkRecord> LinkStore::all(int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "ORDER BY v.from_id, v.type, v.to_id");
    stmt.bindAll(cut);
    std::vector<LinkRecord> links;
    while (stmt.step()) {
        links.push_back(readLink(stmt));
    }
    return links;
}

std::vector<LinkRecord> LinkStore::list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0,
                                        int64_t cut) {
    // Upstream = 이 항목이 주어인 링크(from = id), Downstream = 이 항목이 목적어인 링크(to = id).
    const bool up = dir == KiottySpecDirection_Upstream || dir == KiottySpecDirection_Both;
    const bool down = dir == KiottySpecDirection_Downstream || dir == KiottySpecDirection_Both;
    auto stmt = db_.prepare(std::string(kSelect) +
                            "AND ((?2 = 1 AND v.from_id = ?4) OR (?3 = 1 AND v.to_id = ?4)) AND (?5 = '' OR v.type = ?5)"
                            " ORDER BY v.type, v.from_id, v.to_id");
    stmt.bindAll(cut, up, down, id, typeOr0 != 0 ? std::string(core::name(typeOr0)) : std::string());
    std::vector<LinkRecord> links;
    while (stmt.step()) {
        links.push_back(readLink(stmt));
    }
    return links;
}

std::vector<std::string> LinkStore::targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut) {
    std::vector<std::string> ids;
    for (auto &link : list(fromId, KiottySpecDirection_Upstream, type, cut)) {
        ids.push_back(std::move(link.toId));
    }
    return ids;
}

std::vector<std::string> LinkStore::sources(std::string_view toId, KiottySpecLinkType type, int64_t cut) {
    std::vector<std::string> ids;
    for (auto &link : list(toId, KiottySpecDirection_Downstream, type, cut)) {
        ids.push_back(std::move(link.fromId));
    }
    return ids;
}

std::vector<std::string> LinkStore::traverse(std::string_view id, KiottySpecDirection dir, int32_t depth) {
    const int32_t limit = depth <= 0 ? 64 : depth;
    std::vector<std::string> result;
    std::set<std::string> seen{std::string(id)};
    std::deque<std::pair<std::string, int32_t>> queue{{std::string(id), 0}};
    while (!queue.empty()) {
        auto [current, level] = queue.front();
        queue.pop_front();
        if (level >= limit) {
            continue;
        }
        std::vector<std::string> next;
        if (dir != KiottySpecDirection_Downstream) {
            for (auto &target : targets(current, static_cast<KiottySpecLinkType>(0))) next.push_back(std::move(target));
        }
        if (dir != KiottySpecDirection_Upstream) {
            for (auto &source : sources(current, static_cast<KiottySpecLinkType>(0))) next.push_back(std::move(source));
        }
        for (auto &node : next) {
            if (seen.insert(node).second) {
                result.push_back(node);
                queue.emplace_back(std::move(node), level + 1);
            }
        }
    }
    return result;
}

std::vector<ItemRecord> LinkStore::impactSince(int64_t cpSeq) {
    std::set<std::string> changed;
    for (const auto &change : items_.changedBetween(cpSeq, core::kCurrent)) {
        changed.insert(change.record.id);
    }
    for (const auto &file : files_.changedSince(cpSeq)) {
        changed.insert(file.id);
    }
    // 바뀐 링크의 양 끝도 영향 대상이다.
    auto changedLinks = db_.prepare("SELECT DISTINCT from_id, to_id FROM link_version WHERE cp_seq > ?1");
    changedLinks.bindAll(cpSeq);
    std::set<std::string> linkEnds;
    while (changedLinks.step()) {
        linkEnds.insert(changedLinks.text(0));
        linkEnds.insert(changedLinks.text(1));
    }

    std::set<std::string> affected;
    for (const auto &id : changed) {
        for (const auto &link : list(id, KiottySpecDirection_Both, static_cast<KiottySpecLinkType>(0))) {
            affected.insert(link.fromId == id ? link.toId : link.fromId);
        }
    }
    affected.insert(linkEnds.begin(), linkEnds.end());

    std::vector<ItemRecord> result;
    for (const auto &id : affected) {
        if (changed.count(id) != 0) {
            continue;
        }
        if (auto item = items_.find(id)) {
            result.push_back(std::move(*item));
        }
    }
    return result;
}

std::vector<std::string> LinkStore::modulesOf(std::string_view id, bool &touchesNonModule) {
    std::vector<std::string> modules;
    const auto addDesign = [&](const std::string &designId) {
        const auto design = items_.find(designId);
        if (!design) return;
        if (design->category == KiottySpecItemCategory_DsnModule) {
            modules.push_back(design->id);
        } else {
            touchesNonModule = true;  // 동작 흐름·디렉토리·메모리 정책은 여러 모듈에 걸친다
        }
    };
    if (const auto item = items_.find(id)) {
        switch (item->kind) {
        case KiottySpecItemKind_Design:
            addDesign(item->id);
            break;
        case KiottySpecItemKind_UnitTest:
            for (const auto &target : targets(item->id, KiottySpecLinkType_Verifies)) addDesign(target);
            break;
        default:
            touchesNonModule = true;  // 요구사항·인터페이스 등은 모듈 밖 변경
            break;
        }
    } else {
        files_.get(id);
        for (const auto &target : targets(id, KiottySpecLinkType_Implements)) {
            const auto targetItem = items_.find(target);
            if (targetItem && targetItem->kind == KiottySpecItemKind_Design) {
                addDesign(target);
            } else {
                touchesNonModule = true;
            }
        }
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return modules;
}

ScopeResult LinkStore::scopeOf(std::string_view id) {
    bool touchesNonModule = false;
    const auto modules = modulesOf(id, touchesNonModule);
    std::set<std::string> own(modules.begin(), modules.end());
    std::set<std::string> affected;
    std::deque<std::string> queue(modules.begin(), modules.end());
    while (!queue.empty()) {
        const std::string module = queue.front();
        queue.pop_front();
        for (const auto &dependent : sources(module, KiottySpecLinkType_DependsOn)) {
            if (own.count(dependent) == 0 && affected.insert(dependent).second) {
                queue.push_back(dependent);
            }
        }
    }
    ScopeResult result;
    if (modules.size() > 1) {
        affected.insert(modules.begin(), modules.end());
    }
    result.affectedModules.assign(affected.begin(), affected.end());
    const bool intra = modules.size() == 1 && affected.empty() && !touchesNonModule;
    result.scope = intra ? KiottySpecScope_IntraModule : KiottySpecScope_InterModule;
    return result;
}

std::vector<std::string> LinkStore::suspects(std::string_view failedTargetId) {
    std::vector<std::string> designs;
    std::vector<std::string> result;
    const auto push = [](std::vector<std::string> &into, const std::string &value) {
        if (std::find(into.begin(), into.end(), value) == into.end()) into.push_back(value);
    };
    const auto target = items_.get(failedTargetId);
    if (target.kind == KiottySpecItemKind_Rule) {
        // 코드 검사 위반: 규칙과 링크된 것은 없으므로 모든 소스 파일이 후보다.
        for (const auto &file : files_.list(KiottySpecFileRole_Source)) push(result, file.id);
        return result;
    }
    for (const auto &verified : targets(failedTargetId, KiottySpecLinkType_Verifies)) {
        const auto item = items_.find(verified);
        if (!item) continue;
        if (item->kind == KiottySpecItemKind_Design) {
            push(designs, item->id);
        } else {
            for (const auto &design : sources(item->id, KiottySpecLinkType_Satisfies)) push(designs, design);
            if (item->kind == KiottySpecItemKind_Interface) {
                for (const auto &file : sources(item->id, KiottySpecLinkType_Implements)) push(result, file);
            }
        }
    }
    for (const auto &design : designs) {
        push(result, design);
        for (const auto &file : sources(design, KiottySpecLinkType_Implements)) push(result, file);
    }
    return result;
}

std::string LinkStore::matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind) {
    const auto rows = items_.ofKind(rowKind);
    const auto cols = items_.ofKind(colKind);
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto &link : all()) {
        pairs.emplace(link.fromId, link.toId);
        pairs.emplace(link.toId, link.fromId);
    }
    std::string csv = std::string(core::name(rowKind)) + "\\" + std::string(core::name(colKind));
    for (const auto &col : cols) csv += "," + col.id;
    csv += "\n";
    for (const auto &row : rows) {
        csv += row.id;
        for (const auto &col : cols) csv += pairs.count({row.id, col.id}) != 0 ? ",x" : ",";
        csv += "\n";
    }
    return csv;
}

GuiScanResult LinkStore::scanGuiDraft(std::string_view fileId) {
    const auto file = files_.get(fileId);
    core::require(file.role == KiottySpecFileRole_GuiDraft, KiottySpecStatus_InvalidArg, "GUI 초안 파일이 아님: " + file.id);
    const auto content = files_.read(file);
    core::require(content.has_value(), KiottySpecStatus_Io, "파일을 읽을 수 없음: " + file.path);

    static const std::regex pattern(R"(data-ks-id\s*=\s*["']([^"']+)["'])");
    std::set<std::string> found;
    for (auto it = std::sregex_iterator(content->begin(), content->end(), pattern); it != std::sregex_iterator(); ++it) {
        std::string id = (*it)[1].str();
        const auto dot = id.find('.');
        found.insert(dot == std::string::npos ? id : id.substr(0, dot));  // UI-003.save -> UI-003
    }
    GuiScanResult result;
    for (const auto &id : found) {
        const auto item = items_.find(id);
        if (!item || item->kind != KiottySpecItemKind_Interface) {
            result.unknownIds.push_back(id);
        }
    }
    for (const auto &viewId : targets(file.id, KiottySpecLinkType_Implements)) {
        if (found.count(viewId) == 0) {
            if (auto view = items_.find(viewId)) result.missingViews.push_back(std::move(*view));
        }
    }
    return result;
}

void LinkStore::writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed) {
    db_.prepare("INSERT INTO link_version (from_id, to_id, type, cp_seq, removed) VALUES (?1, ?2, ?3, ?4, ?5)"
                " ON CONFLICT(from_id, to_id, type, cp_seq) DO UPDATE SET removed = excluded.removed")
        .bindAll(fromId, toId, core::name(type), core::kPending, removed)
        .run();
}

} // namespace kiotty::base
