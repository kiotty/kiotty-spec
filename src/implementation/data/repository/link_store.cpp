#include "implementation/data/repository/link_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <algorithm>
#include <deque>
#include <map>
#include <regex>
#include <set>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kSelect =
    "SELECT v.from_id, v.to_id, v.type, v.cp_seq FROM link_version v"
    " JOIN (SELECT from_id, to_id, type, MAX(cp_seq) AS m FROM link_version WHERE cp_seq <= ?1"
    " GROUP BY from_id, to_id, type) l"
    " ON v.from_id = l.from_id AND v.to_id = l.to_id AND v.type = l.type AND v.cp_seq = l.m WHERE v.removed = 0 ";

LinkRecord readLink(const Statement &stmt) {
    LinkRecord record;
    record.fromId = stmt.text(0);
    record.toId = stmt.text(1);
    parse(stmt.text(2), record.type);
    record.cpSeq = stmt.integer(3);
    return record;
}

struct Node {
    bool isFile = false;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category{};
    KiottySpecFileRole role{};
};

bool isFunctional(const Node &n) {
    return !n.isFile && n.kind == KIOTTY_SPEC_ITEM_KIND_REQUIREMENT && n.category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
}
bool isItem(const Node &n, KiottySpecItemKind kind) { return !n.isFile && n.kind == kind; }
bool isModule(const Node &n) { return isItem(n, KIOTTY_SPEC_ITEM_KIND_DESIGN) && n.category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE; }

bool allowed(KiottySpecLinkType type, const Node &from, const Node &to) {
    switch (type) {
    case KIOTTY_SPEC_LINK_TYPE_SATISFIES:
        return (isItem(from, KIOTTY_SPEC_ITEM_KIND_INTERFACE) && isFunctional(to)) ||
               (isItem(from, KIOTTY_SPEC_ITEM_KIND_DESIGN) &&
                (isItem(to, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT) || isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE)));
    case KIOTTY_SPEC_LINK_TYPE_VERIFIES:
        return (isItem(from, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST) && isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN)) ||
               (isItem(from, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST) && isFunctional(to)) ||
               (isItem(from, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST) && isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE));
    case KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS:
        return (from.isFile && (from.role == KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT || from.role == KIOTTY_SPEC_FILE_ROLE_C_HEADER) &&
                isItem(to, KIOTTY_SPEC_ITEM_KIND_INTERFACE)) ||
               (from.isFile && from.role == KIOTTY_SPEC_FILE_ROLE_SOURCE && isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN));
    case KIOTTY_SPEC_LINK_TYPE_COVERS:
        return isItem(from, KIOTTY_SPEC_ITEM_KIND_MILESTONE) &&
               (isItem(to, KIOTTY_SPEC_ITEM_KIND_DESIGN) || isItem(to, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST));
    case KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON:
        return isModule(from) && isModule(to);
    }
    return false;
}

constexpr std::string_view kAllowedHelp =
    "허용되는 링크: interface satisfies requirement(functional) | design satisfies requirement/interface | "
    "unit_test verifies design | system_test verifies requirement(functional) | e2e_test verifies interface | "
    "file(gui_draft/c_header) implements interface | file(source) implements design | "
    "milestone covers design/unit_test | design(module) depends_on design(module)";

}

void LinkStore::validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    require(fromId != toId, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "자기 자신으로의 링크는 만들 수 없음");
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
        fail(KIOTTY_SPEC_STATUS_NOT_FOUND, "항목·파일 없음: " + std::string(id));
    };
    const Node from = resolve(fromId);
    const Node to = resolve(toId);
    require(allowed(type, from, to), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                  std::string(fromId) + " " + std::string(name(type)) + " " + std::string(toId) +
                      " 는 허용되지 않음. " + std::string(kAllowedHelp));
}

void LinkStore::add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    validate(fromId, toId, type);
    if (!exists(fromId, toId, type)) {
        writeVersion(fromId, toId, type, false);
    }
}

void LinkStore::remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type) {
    require(exists(fromId, toId, type), KIOTTY_SPEC_STATUS_NOT_FOUND,
                  "링크 없음: " + std::string(fromId) + " " + std::string(name(type)) + " " + std::string(toId));
    writeVersion(fromId, toId, type, true);
}

void LinkStore::removeAllFor(std::string_view id) {
    for (const auto &link : list(id, KIOTTY_SPEC_DIRECTION_BOTH, static_cast<KiottySpecLinkType>(0))) {
        writeVersion(link.fromId, link.toId, link.type, true);
    }
}

bool LinkStore::exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut) {
    auto stmt = db_.prepare(std::string(kSelect) + "AND v.from_id = ?2 AND v.to_id = ?3 AND v.type = ?4");
    stmt.bindAll(cut, fromId, toId, name(type));
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
    const bool up = dir == KIOTTY_SPEC_DIRECTION_UPSTREAM || dir == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool down = dir == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || dir == KIOTTY_SPEC_DIRECTION_BOTH;
    auto stmt = db_.prepare(std::string(kSelect) +
                            "AND ((?2 = 1 AND v.from_id = ?4) OR (?3 = 1 AND v.to_id = ?4)) AND (?5 = '' OR v.type = ?5)"
                            " ORDER BY v.type, v.from_id, v.to_id");
    stmt.bindAll(cut, up, down, id, typeOr0 != 0 ? std::string(name(typeOr0)) : std::string());
    std::vector<LinkRecord> links;
    while (stmt.step()) {
        links.push_back(readLink(stmt));
    }
    return links;
}

std::vector<std::string> LinkStore::targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut) {
    std::vector<std::string> ids;
    for (auto &link : list(fromId, KIOTTY_SPEC_DIRECTION_UPSTREAM, type, cut)) {
        ids.push_back(std::move(link.toId));
    }
    return ids;
}

std::vector<std::string> LinkStore::sources(std::string_view toId, KiottySpecLinkType type, int64_t cut) {
    std::vector<std::string> ids;
    for (auto &link : list(toId, KIOTTY_SPEC_DIRECTION_DOWNSTREAM, type, cut)) {
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
        if (dir != KIOTTY_SPEC_DIRECTION_DOWNSTREAM) {
            for (auto &target : targets(current, static_cast<KiottySpecLinkType>(0))) next.push_back(std::move(target));
        }
        if (dir != KIOTTY_SPEC_DIRECTION_UPSTREAM) {
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
    for (const auto &change : items_.changedBetween(cpSeq, kCurrent)) {
        changed.insert(change.record.id);
    }
    for (const auto &file : files_.changedSince(cpSeq)) {
        changed.insert(file.id);
    }
    auto changedLinks = db_.prepare("SELECT DISTINCT from_id, to_id FROM link_version WHERE cp_seq > ?1");
    changedLinks.bindAll(cpSeq);
    std::set<std::string> linkEnds;
    while (changedLinks.step()) {
        linkEnds.insert(changedLinks.text(0));
        linkEnds.insert(changedLinks.text(1));
    }

    std::set<std::string> affected;
    for (const auto &id : changed) {
        for (const auto &link : list(id, KIOTTY_SPEC_DIRECTION_BOTH, static_cast<KiottySpecLinkType>(0))) {
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
        if (design->category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE) {
            modules.push_back(design->id);
        } else {
            touchesNonModule = true;
        }
    };
    if (const auto item = items_.find(id)) {
        switch (item->kind) {
        case KIOTTY_SPEC_ITEM_KIND_DESIGN:
            addDesign(item->id);
            break;
        case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST:
            for (const auto &target : targets(item->id, KIOTTY_SPEC_LINK_TYPE_VERIFIES)) addDesign(target);
            break;
        default:
            touchesNonModule = true;
            break;
        }
    } else {
        files_.get(id);
        for (const auto &target : targets(id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS)) {
            const auto targetItem = items_.find(target);
            if (targetItem && targetItem->kind == KIOTTY_SPEC_ITEM_KIND_DESIGN) {
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
        for (const auto &dependent : sources(module, KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON)) {
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
    result.scope = intra ? KIOTTY_SPEC_SCOPE_INTRA_MODULE : KIOTTY_SPEC_SCOPE_INTER_MODULE;
    return result;
}

std::vector<std::string> LinkStore::suspects(std::string_view failedTargetId) {
    std::vector<std::string> designs;
    std::vector<std::string> result;
    const auto push = [](std::vector<std::string> &into, const std::string &value) {
        if (std::find(into.begin(), into.end(), value) == into.end()) into.push_back(value);
    };
    const auto target = items_.get(failedTargetId);
    if (target.kind == KIOTTY_SPEC_ITEM_KIND_RULE) {
        for (const auto &file : files_.list(KIOTTY_SPEC_FILE_ROLE_SOURCE)) push(result, file.id);
        return result;
    }
    for (const auto &verified : targets(failedTargetId, KIOTTY_SPEC_LINK_TYPE_VERIFIES)) {
        const auto item = items_.find(verified);
        if (!item) continue;
        if (item->kind == KIOTTY_SPEC_ITEM_KIND_DESIGN) {
            push(designs, item->id);
        } else {
            for (const auto &design : sources(item->id, KIOTTY_SPEC_LINK_TYPE_SATISFIES)) push(designs, design);
            if (item->kind == KIOTTY_SPEC_ITEM_KIND_INTERFACE) {
                for (const auto &file : sources(item->id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS)) push(result, file);
            }
        }
    }
    for (const auto &design : designs) {
        push(result, design);
        for (const auto &file : sources(design, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS)) push(result, file);
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
    std::string csv = std::string(name(rowKind)) + "\\" + std::string(name(colKind));
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
    require(file.role == KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "GUI 초안 파일이 아님: " + file.id);
    const auto content = files_.read(file);
    require(content.has_value(), KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "파일을 읽을 수 없음: " + file.path);

    static const std::regex pattern(R"(data-ks-id\s*=\s*["']([^"']+)["'])");
    std::set<std::string> found;
    for (auto it = std::sregex_iterator(content->begin(), content->end(), pattern); it != std::sregex_iterator(); ++it) {
        std::string id = (*it)[1].str();
        const auto dot = id.find('.');
        found.insert(dot == std::string::npos ? id : id.substr(0, dot));
    }
    GuiScanResult result;
    for (const auto &id : found) {
        const auto item = items_.find(id);
        if (!item || item->kind != KIOTTY_SPEC_ITEM_KIND_INTERFACE) {
            result.unknownIds.push_back(id);
        }
    }
    for (const auto &viewId : targets(file.id, KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS)) {
        if (found.count(viewId) == 0) {
            if (auto view = items_.find(viewId)) result.missingViews.push_back(std::move(*view));
        }
    }
    return result;
}

void LinkStore::writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed) {
    db_.prepare("INSERT INTO link_version (from_id, to_id, type, cp_seq, removed) VALUES (?1, ?2, ?3, ?4, ?5)"
                " ON CONFLICT(from_id, to_id, type, cp_seq) DO UPDATE SET removed = excluded.removed")
        .bindAll(fromId, toId, name(type), kPending, removed)
        .run();
}

}
