#include "implementation/data/repository/item_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"

#include <set>

namespace kiotty::spec::implementation {

namespace {

constexpr int ID_WIDTH = 3;
constexpr int MAX_PARENT_DEPTH = 64;

constexpr char FIND_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0 AND v.item_id = ?2 ORDER BY v.item_id";
constexpr char LIST_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0"
    " AND (?2 = '' OR v.kind = ?2) AND (?3 = '' OR v.category = ?3)"
    " AND (?4 IS NULL OR COALESCE(v.parent_id, '') = ?4)"
    " AND (?5 IS NULL OR v.title LIKE ?5 ESCAPE '\\' OR v.body LIKE ?5 ESCAPE '\\')"
    " ORDER BY v.kind, v.parent_id, v.seq, v.item_id";
constexpr char CHILDREN_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0 AND v.parent_id = ?2 ORDER BY v.seq, v.item_id";
constexpr char NEXT_SEQ_SQL[] =
    "SELECT COALESCE(MAX(v.seq), 0) + 1"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0 AND COALESCE(v.parent_id, '') = ?2 AND v.kind = ?3";
constexpr char CHANGED_IDS_SQL[] =
    "SELECT DISTINCT item_id FROM item_version WHERE cp_seq > ?1 AND cp_seq <= ?2 ORDER BY item_id";
constexpr char VERSION_AT_SQL[] =
    "SELECT v.item_id, v.cp_seq, v.wseq, v.kind, v.category, v.parent_id, v.seq, v.title, v.body, v.removed"
    " FROM item_version v WHERE v.item_id = ?1 AND v.cp_seq <= ?2 ORDER BY v.cp_seq DESC LIMIT 1";
constexpr char REMOVED_AT_SQL[] =
    "SELECT removed FROM item_version WHERE item_id = ?1 AND cp_seq <= ?2 ORDER BY cp_seq DESC LIMIT 1";
constexpr char WRITE_SQL[] =
    "INSERT INTO item_version (item_id, cp_seq, wseq, kind, category, parent_id, seq, title, body, removed)"
    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"
    " ON CONFLICT(item_id, cp_seq) DO UPDATE SET wseq = excluded.wseq, kind = excluded.kind,"
    " category = excluded.category, parent_id = excluded.parent_id, seq = excluded.seq,"
    " title = excluded.title, body = excluded.body, removed = excluded.removed";

ItemRecord readItem(const Row &row)
{
    ItemRecord record;
    record.id = std::string(viewOf(row.text(0)));
    record.cpSeq = row.integer(1);
    record.wseq = row.integer(2);
    parse(viewOf(row.text(3)), record.kind);
    parse(viewOf(row.text(4)), record.category);
    record.parentId = std::string(viewOf(row.text(5)));
    record.seq = static_cast<int32_t>(row.integer(6));
    record.title = std::string(viewOf(row.text(7)));
    record.body = std::string(viewOf(row.text(8)));
    record.removed = row.integer(9) != 0;
    return record;
}

ItemListResult collect(Storage &storage, Query &query)
{
    std::vector<ItemRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        records.push_back(readItem(query.row()));
        stepped = query.step();
    }
    if (!stepped) { return ItemListResult::failure(storage.check(stepped.code)); }
    return ItemListResult::success(std::move(records));
}

void bindOptional(Query &query, int index, const std::optional<std::string> &value)
{
    if (value) {
        query.bind(index, spanOf(*value));
        return;
    }
    query.bind(index, nullptr);
}

std::optional<std::string> parentOf(const ItemRecord &record)
{
    if (record.parentId.empty()) { return std::nullopt; }
    return record.parentId;
}

bool needsCategory(KiottySpecItemKind kind)
{
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT:
    case KIOTTY_SPEC_ITEM_KIND_RULE:
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE:
    case KIOTTY_SPEC_ITEM_KIND_DESIGN:
        return true;
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST:
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST:
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST:
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE:
        return false;
    }
    return false;
}

}

TextResult ItemStore::create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                             const std::optional<std::string> &parentId, std::string_view title, std::string_view body)
{
    if (idPrefix(kind) == "ITEM") { return TextResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 kind")})); }
    const KiottySpecStatus categorized = validateCategory(kind, category);
    if (categorized != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(categorized); }
    if (trim(title).empty()) { return TextResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("title 이 비어 있음")})); }
    ItemRecord record;
    record.kind = kind;
    record.category = category;
    record.parentId = parentId.value_or("");
    record.title = std::string(trim(title));
    record.body = std::string(body);
    Savepoint savepoint(_storage.connection());
    const KiottySpecStatus begun = _storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(begun); }
    const auto id = insert(std::move(record), parentId);
    if (!id) { return id; }
    const KiottySpecStatus committed = _storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(committed); }
    return id;
}

OptionalItemResult ItemStore::find(std::string_view id, int64_t cut)
{
    auto query = _storage.prepare(literalText(FIND_SQL));
    if (!query) { return OptionalItemResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(id));
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalItemResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalItemResult::success(std::nullopt); }
    return OptionalItemResult::success(readItem(query.value.row()));
}

ItemResult ItemStore::get(std::string_view id, int64_t cut)
{
    auto found = find(id, cut);
    if (!found) { return ItemResult::failure(found.code); }
    if (!found.value) { return ItemResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("항목 없음: "), spanOf(id)})); }
    return ItemResult::success(std::move(*found.value));
}

ItemListResult ItemStore::list(const ItemQuery &query)
{
    auto prepared = _storage.prepare(literalText(LIST_SQL));
    if (!prepared) { return ItemListResult::failure(prepared.code); }
    const std::string_view kind = query.kind != 0 ? name(query.kind) : std::string_view();
    const std::string_view category = query.category != 0 ? name(query.category) : std::string_view();
    std::optional<std::string> pattern;
    if (query.text) { pattern = "%" + escapeLike(*query.text) + "%"; }
    prepared.value.bind(1, query.cut);
    prepared.value.bind(2, spanOf(kind));
    prepared.value.bind(3, spanOf(category));
    bindOptional(prepared.value, 4, query.parentId);
    bindOptional(prepared.value, 5, pattern);
    return collect(_storage, prepared.value);
}

ItemListResult ItemStore::ofKind(KiottySpecItemKind kind, int64_t cut)
{
    ItemQuery query;
    query.kind = kind;
    query.cut = cut;
    return list(query);
}

ItemListResult ItemStore::children(std::string_view id, bool recursive, int64_t cut)
{
    const auto root = get(id, cut);
    if (!root) { return ItemListResult::failure(root.code); }
    std::vector<ItemRecord> result;
    std::vector<std::string> frontier{std::string(id)};
    std::set<std::string> seen{std::string(id)};
    while (!frontier.empty()) {
        const std::string parent = frontier.back();
        frontier.pop_back();
        auto found = childrenOf(parent, cut);
        if (!found) { return found; }
        for (ItemRecord &child : found.value) {
            if (!seen.insert(child.id).second) { continue; }
            if (recursive) { frontier.push_back(child.id); }
            result.push_back(std::move(child));
        }
    }
    return ItemListResult::success(std::move(result));
}

ItemListResult ItemStore::search(std::string_view query, size_t limit)
{
    ItemQuery itemQuery;
    itemQuery.text = std::string(query);
    auto records = list(itemQuery);
    if (!records) { return records; }
    const bool isOverLimit = limit > 0 && records.value.size() > limit;
    if (isOverLimit) { records.value.resize(limit); }
    return records;
}

ItemChangeListResult ItemStore::changedBetween(int64_t fromSeq, int64_t toSeq)
{
    auto ids = _storage.prepare(literalText(CHANGED_IDS_SQL));
    if (!ids) { return ItemChangeListResult::failure(ids.code); }
    ids.value.bind(1, fromSeq);
    ids.value.bind(2, toSeq);
    std::vector<ItemChange> changes;
    auto stepped = ids.value.step();
    while (stepped.value) {
        const std::string id(viewOf(ids.value.row().text(0)));
        auto change = changeOf(id, fromSeq, toSeq);
        if (!change) { return ItemChangeListResult::failure(change.code); }
        if (!change.value.record.id.empty()) { changes.push_back(std::move(change.value)); }
        stepped = ids.value.step();
    }
    if (!stepped) { return ItemChangeListResult::failure(_storage.check(stepped.code)); }
    return ItemChangeListResult::success(std::move(changes));
}

KiottySpecStatus ItemStore::update(std::string_view id, const std::optional<std::string> &title,
                                   const std::optional<std::string> &body)
{
    auto record = get(id);
    if (!record) { return record.code; }
    if (title) {
        if (trim(*title).empty()) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("title 이 비어 있음")}); }
        record.value.title = std::string(trim(*title));
    }
    if (body) { record.value.body = *body; }
    return writeVersion(record.value);
}

KiottySpecStatus ItemStore::setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq)
{
    auto record = get(id);
    if (!record) { return record.code; }
    const KiottySpecStatus validated = validateParent(id, parentId);
    if (validated != KIOTTY_SPEC_STATUS_SUCCESS) { return validated; }
    record.value.parentId = parentId.value_or("");
    record.value.seq = seq;
    return writeVersion(record.value);
}

KiottySpecStatus ItemStore::remove(std::string_view id)
{
    auto record = get(id);
    if (!record) { return record.code; }
    record.value.removed = true;
    return writeVersion(record.value);
}

TextResult ItemStore::insert(ItemRecord record, const std::optional<std::string> &parentId)
{
    auto id = _meta.nextId(idPrefix(record.kind), ID_WIDTH);
    if (!id) { return id; }
    record.id = id.value;
    const KiottySpecStatus validated = validateParent(record.id, parentId);
    if (validated != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(validated); }
    const auto seq = nextSiblingSeq(record);
    if (!seq) { return TextResult::failure(seq.code); }
    record.seq = static_cast<int32_t>(seq.value);
    const KiottySpecStatus written = writeVersion(record);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(written); }
    return id;
}

SequenceResult ItemStore::nextSiblingSeq(const ItemRecord &record)
{
    auto query = _storage.prepare(literalText(NEXT_SEQ_SQL));
    if (!query) { return SequenceResult::failure(query.code); }
    query.value.bind(1, kCurrent);
    query.value.bind(2, spanOf(record.parentId));
    query.value.bind(3, spanOf(name(record.kind)));
    const auto stepped = query.value.step();
    if (!stepped) { return SequenceResult::failure(_storage.check(stepped.code)); }
    return SequenceResult::success(query.value.row().integer(0));
}

ItemListResult ItemStore::childrenOf(std::string_view parentId, int64_t cut)
{
    auto query = _storage.prepare(literalText(CHILDREN_SQL));
    if (!query) { return ItemListResult::failure(query.code); }
    query.value.bind(1, cut);
    query.value.bind(2, spanOf(parentId));
    return collect(_storage, query.value);
}

Result<ItemChange, KiottySpecStatus> ItemStore::changeOf(std::string_view id, int64_t fromSeq, int64_t toSeq)
{
    using ChangeResult = Result<ItemChange, KiottySpecStatus>;
    auto latest = _storage.prepare(literalText(VERSION_AT_SQL));
    if (!latest) { return ChangeResult::failure(latest.code); }
    latest.value.bind(1, spanOf(id));
    latest.value.bind(2, toSeq);
    const auto found = latest.value.step();
    if (!found) { return ChangeResult::failure(_storage.check(found.code)); }
    if (!found.value) { return ChangeResult::success(ItemChange{}); }
    ItemChange change;
    change.record = readItem(latest.value.row());
    auto before = _storage.prepare(literalText(REMOVED_AT_SQL));
    if (!before) { return ChangeResult::failure(before.code); }
    before.value.bind(1, spanOf(id));
    before.value.bind(2, fromSeq);
    const auto existed = before.value.step();
    if (!existed) { return ChangeResult::failure(_storage.check(existed.code)); }
    change.added = !existed.value || before.value.row().integer(0) != 0;
    return ChangeResult::success(std::move(change));
}

KiottySpecStatus ItemStore::validateCategory(KiottySpecItemKind kind, KiottySpecItemCategory category)
{
    const Span<const char> kindLabel = spanOf(label(kind));
    if (needsCategory(kind)) {
        const bool matches = category != KIOTTY_SPEC_ITEM_CATEGORY_NONE && kindOfCategory(category) == kind;
        if (matches) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {kindLabel, literalText(" 은 그 종류에 맞는 category 가 필요함")});
    }
    if (category == KIOTTY_SPEC_ITEM_CATEGORY_NONE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {kindLabel, literalText(" 은 category 를 갖지 않음")});
}

KiottySpecStatus ItemStore::validateParent(std::string_view id, const std::optional<std::string> &parentId)
{
    const bool hasParent = parentId && !parentId->empty();
    if (!hasParent) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (*parentId == id) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("자기 자신을 상위 항목으로 지정할 수 없음")}); }
    std::string cursor = *parentId;
    for (int depth = 0; depth < MAX_PARENT_DEPTH && !cursor.empty(); ++depth) {
        const auto parent = find(cursor);
        if (!parent) { return parent.code; }
        if (!parent.value) { return _storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("상위 항목 없음: "), spanOf(cursor)}); }
        if (parent.value->id == id) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("상위 관계가 순환함")}); }
        cursor = parent.value->parentId;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemStore::writeVersion(const ItemRecord &record)
{
    const auto writeSeq = _meta.nextWriteSeq();
    if (!writeSeq) { return writeSeq.code; }
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(record.id));
    query.value.bind(2, kPending);
    query.value.bind(3, writeSeq.value);
    query.value.bind(4, spanOf(name(record.kind)));
    query.value.bind(5, spanOf(name(record.category)));
    bindOptional(query.value, 6, parentOf(record));
    query.value.bind(7, static_cast<int64_t>(record.seq));
    query.value.bind(8, spanOf(record.title));
    query.value.bind(9, spanOf(record.body));
    query.value.bind(10, static_cast<int64_t>(record.removed ? 1 : 0));
    return _storage.check(query.value.run());
}

std::string ItemStore::bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category) {
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY) {
            return "## 진술\n\n## 지표\n\n## 목표값\n\n## 측정 방법\n";
        }
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT) {
            return "## 진술\n\n## 대상 (OS / 언어 / 하드웨어)\n";
        }
        return "## 진술\n<한 문장>\n\n## 근거 시나리오\n<요약>\n\n## 인수 조건\n"
               "- 전제 조건: ... / 행동: ... / 기대 결과: ...\n";
    case KIOTTY_SPEC_ITEM_KIND_RULE:
        return "## 규칙\n\n## 이유\n\n## 검사 방법\n";
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW) {
            return "## 목적\n\n## 진입 경로\n\n## 표시 데이터\n\n## 상호작용\n- \n\n## 표시 상태\n- 빈 화면:\n- 로딩:\n- 에러:\n";
        }
        return "## 시그니처\n```c\n```\n\n## 사전조건\n\n## 사후조건\n\n## 에러 코드\n\n## 소유권\n";
    case KIOTTY_SPEC_ITEM_KIND_DESIGN:
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW) {
            return "## 참여 모듈\n\n## 순서\n1. \n\n## 데이터 흐름\n";
        }
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE) {
            return "## 책임\n\n## 모듈 인터페이스\n```cpp\n```\n\n## 멤버 변수\n\n## 의존 모듈\n";
        }
        return "## 내용\n";
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST:
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST:
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST:
        return "## 전제 조건\n\n## 행동\n\n## 기대 결과\n";
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE:
        return "## 목표\n\n## 완료 기준\n";
    }
    return "";
}

}
