#include "implementation/base/item_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <set>

namespace kiotty::base {

using core::ItemRecord;
using core::kPending;
using core::trim;

namespace {

constexpr std::string_view kColumns = "v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed";

// cut 시점에 유효한 버전만 남기는 FROM 절. ?1 = cut
constexpr std::string_view kLatestFrom =
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m ";

ItemRecord readItem(const core::Statement &stmt) {
    ItemRecord record;
    record.id = stmt.text(0);
    record.cpSeq = stmt.integer(1);
    record.wseq = stmt.integer(2);
    core::parse(stmt.text(3), record.kind);
    core::parse(stmt.text(4), record.category);
    record.parentId = stmt.optText(5).value_or("");
    record.seq = static_cast<int32_t>(stmt.integer(6));
    record.title = stmt.text(7);
    record.body = stmt.text(8);
    record.removed = stmt.integer(9) != 0;
    return record;
}

std::string latestSql(std::string_view where, std::string_view order = "ORDER BY v.item_id") {
    return "SELECT " + std::string(kColumns) + std::string(kLatestFrom) + "WHERE v.removed = 0 " + std::string(where) +
           " " + std::string(order);
}

void validateCategory(KiottySpecItemKind kind, KiottySpecItemCategory category) {
    const bool needsCategory = kind == KiottySpecItemKind_Requirement || kind == KiottySpecItemKind_Rule ||
                               kind == KiottySpecItemKind_Interface || kind == KiottySpecItemKind_Design;
    if (needsCategory) {
        core::require(category != KiottySpecItemCategory_None && core::kindOfCategory(category) == kind,
                      KiottySpecStatus_InvalidArg,
                      std::string(core::label(kind)) + " 은 그 종류에 맞는 category 가 필요함");
    } else {
        core::require(category == KiottySpecItemCategory_None, KiottySpecStatus_InvalidArg,
                      std::string(core::label(kind)) + " 은 category 를 갖지 않음");
    }
}

} // namespace

std::string ItemStore::create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                              const std::optional<std::string> &parentId, std::string_view title,
                              std::string_view body) {
    core::require(core::idPrefix(kind) != "ITEM", KiottySpecStatus_InvalidArg, "알 수 없는 kind");
    validateCategory(kind, category);
    core::require(!trim(title).empty(), KiottySpecStatus_InvalidArg, "title 이 비어 있음");
    core::Savepoint savepoint(db_);
    ItemRecord record;
    record.id = meta_.nextId(core::idPrefix(kind), 3);
    validateParent(record.id, parentId);
    record.kind = kind;
    record.category = category;
    record.parentId = parentId.value_or("");
    record.title = std::string(trim(title));
    record.body = std::string(body);

    auto seqStmt = db_.prepare("SELECT COALESCE(MAX(v.seq), 0) + 1 " + std::string(kLatestFrom) +
                               "WHERE v.removed = 0 AND COALESCE(v.parent_id, '') = ?2 AND v.kind = ?3");
    seqStmt.bindAll(core::kCurrent, record.parentId, core::name(kind));
    seqStmt.step();
    record.seq = static_cast<int32_t>(seqStmt.integer(0));

    writeVersion(record);
    savepoint.commit();
    return record.id;
}

std::optional<ItemRecord> ItemStore::find(std::string_view id, int64_t cut) {
    auto stmt = db_.prepare(latestSql("AND v.item_id = ?2"));
    stmt.bindAll(cut, id);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readItem(stmt);
}

ItemRecord ItemStore::get(std::string_view id, int64_t cut) {
    auto record = find(id, cut);
    core::require(record.has_value(), KiottySpecStatus_NotFound, "항목 없음: " + std::string(id));
    return *record;
}

std::vector<ItemRecord> ItemStore::list(const ItemQuery &query) {
    constexpr std::string_view where =
        "AND (?2 = '' OR v.kind = ?2) AND (?3 = '' OR v.category = ?3)"
        " AND (?4 IS NULL OR COALESCE(v.parent_id, '') = ?4)"
        " AND (?5 IS NULL OR v.title LIKE ?5 ESCAPE '\\' OR v.body LIKE ?5 ESCAPE '\\')";
    auto stmt = db_.prepare(latestSql(where, "ORDER BY v.kind, v.parent_id, v.seq, v.item_id"));
    const std::string kind = query.kind != 0 ? std::string(core::name(query.kind)) : std::string();
    const std::string category = query.category != 0 ? std::string(core::name(query.category)) : std::string();
    const std::optional<std::string> pattern =
        query.text ? std::optional<std::string>("%" + core::escapeLike(*query.text) + "%") : std::nullopt;
    stmt.bindAll(query.cut, kind, category, query.parentId, pattern);
    std::vector<ItemRecord> records;
    while (stmt.step()) {
        records.push_back(readItem(stmt));
    }
    return records;
}

std::vector<ItemRecord> ItemStore::ofKind(KiottySpecItemKind kind, int64_t cut) {
    ItemQuery query;
    query.kind = kind;
    query.cut = cut;
    return list(query);
}

std::vector<ItemRecord> ItemStore::children(std::string_view id, bool recursive, int64_t cut) {
    get(id, cut);
    std::vector<ItemRecord> result;
    std::vector<std::string> frontier{std::string(id)};
    std::set<std::string> seen{std::string(id)};
    while (!frontier.empty()) {
        const std::string parent = frontier.back();
        frontier.pop_back();
        auto stmt = db_.prepare(latestSql("AND v.parent_id = ?2", "ORDER BY v.seq, v.item_id"));
        stmt.bindAll(cut, parent);
        while (stmt.step()) {
            auto child = readItem(stmt);
            if (!seen.insert(child.id).second) {
                continue;
            }
            if (recursive) {
                frontier.push_back(child.id);
            }
            result.push_back(std::move(child));
        }
    }
    return result;
}

std::vector<ItemRecord> ItemStore::search(std::string_view query, size_t limit) {
    ItemQuery itemQuery;
    itemQuery.text = std::string(query);
    auto records = list(itemQuery);
    if (limit > 0 && records.size() > limit) {
        records.resize(limit);
    }
    return records;
}

std::vector<ItemChange> ItemStore::changedBetween(int64_t fromSeq, int64_t toSeq) {
    auto ids = db_.prepare("SELECT DISTINCT item_id FROM item_version WHERE cp_seq > ?1 AND cp_seq <= ?2 ORDER BY item_id");
    ids.bindAll(fromSeq, toSeq);
    std::vector<ItemChange> changes;
    while (ids.step()) {
        const std::string id = ids.text(0);
        auto latest = db_.prepare("SELECT " + std::string(kColumns) +
                                  " FROM item_version v WHERE v.item_id = ?1 AND v.cp_seq <= ?2 ORDER BY v.cp_seq DESC LIMIT 1");
        latest.bindAll(id, toSeq);
        if (!latest.step()) {
            continue;
        }
        ItemChange change;
        change.record = readItem(latest);
        auto before = db_.prepare("SELECT removed FROM item_version WHERE item_id = ?1 AND cp_seq <= ?2 ORDER BY cp_seq DESC LIMIT 1");
        before.bindAll(id, fromSeq);
        change.added = !before.step() || before.integer(0) != 0;
        changes.push_back(std::move(change));
    }
    return changes;
}

void ItemStore::update(std::string_view id, const std::optional<std::string> &title,
                       const std::optional<std::string> &body) {
    auto record = get(id);
    if (title) {
        core::require(!trim(*title).empty(), KiottySpecStatus_InvalidArg, "title 이 비어 있음");
        record.title = std::string(trim(*title));
    }
    if (body) {
        record.body = *body;
    }
    writeVersion(record);
}

void ItemStore::setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq) {
    auto record = get(id);
    validateParent(id, parentId);
    record.parentId = parentId.value_or("");
    record.seq = seq;
    writeVersion(record);
}

void ItemStore::remove(std::string_view id) {
    auto record = get(id);
    record.removed = true;
    writeVersion(record);
}

void ItemStore::writeVersion(const ItemRecord &record) {
    db_.prepare(
           "INSERT INTO item_version (item_id, cp_seq, wseq, kind, category, parent_id, seq, title, body, removed)"
           " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"
           " ON CONFLICT(item_id, cp_seq) DO UPDATE SET wseq = excluded.wseq, kind = excluded.kind,"
           " category = excluded.category, parent_id = excluded.parent_id, seq = excluded.seq,"
           " title = excluded.title, body = excluded.body, removed = excluded.removed")
        .bindAll(record.id, kPending, meta_.nextWriteSeq(), core::name(record.kind), core::name(record.category),
                 record.parentId.empty() ? std::optional<std::string>() : std::optional<std::string>(record.parentId),
                 record.seq, record.title, record.body, record.removed)
        .run();
}

void ItemStore::validateParent(std::string_view id, const std::optional<std::string> &parentId) {
    if (!parentId || parentId->empty()) {
        return;
    }
    core::require(*parentId != id, KiottySpecStatus_InvalidArg, "자기 자신을 상위 항목으로 지정할 수 없음");
    std::string cursor = *parentId;
    for (int depth = 0; depth < 64 && !cursor.empty(); ++depth) {
        const auto parent = find(cursor);
        core::require(parent.has_value(), KiottySpecStatus_NotFound, "상위 항목 없음: " + cursor);
        core::require(parent->id != id, KiottySpecStatus_InvalidArg, "상위 관계가 순환함");
        cursor = parent->parentId;
    }
}

std::string ItemStore::bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category) {
    switch (kind) {
    case KiottySpecItemKind_Requirement:
        if (category == KiottySpecItemCategory_ReqQuality) {
            return "## 진술\n\n## 지표\n\n## 목표값\n\n## 측정 방법\n";
        }
        if (category == KiottySpecItemCategory_ReqEnvironment) {
            return "## 진술\n\n## 대상 (OS / 언어 / 하드웨어)\n";
        }
        return "## 진술\n<한 문장>\n\n## 근거 시나리오\n<요약>\n\n## 인수 조건\n"
               "- 전제 조건: ... / 행동: ... / 기대 결과: ...\n";
    case KiottySpecItemKind_Rule:
        return "## 규칙\n\n## 이유\n\n## 검사 방법\n";
    case KiottySpecItemKind_Interface:
        if (category == KiottySpecItemCategory_UiView) {
            return "## 목적\n\n## 진입 경로\n\n## 표시 데이터\n\n## 상호작용\n- \n\n## 표시 상태\n- 빈 화면:\n- 로딩:\n- 에러:\n";
        }
        return "## 시그니처\n```c\n```\n\n## 사전조건\n\n## 사후조건\n\n## 에러 코드\n\n## 소유권\n";
    case KiottySpecItemKind_Design:
        if (category == KiottySpecItemCategory_DsnFlow) {
            return "## 참여 모듈\n\n## 순서\n1. \n\n## 데이터 흐름\n";
        }
        if (category == KiottySpecItemCategory_DsnModule) {
            return "## 책임\n\n## 모듈 인터페이스\n```cpp\n```\n\n## 멤버 변수\n\n## 의존 모듈\n";
        }
        return "## 내용\n";
    case KiottySpecItemKind_UnitTest:
    case KiottySpecItemKind_SystemTest:
    case KiottySpecItemKind_E2eTest:
        return "## 전제 조건\n\n## 행동\n\n## 기대 결과\n";
    case KiottySpecItemKind_Milestone:
        return "## 목표\n\n## 완료 기준\n";
    }
    return "";
}

} // namespace kiotty::base
