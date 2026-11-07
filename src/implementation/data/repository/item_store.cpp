#include "implementation/data/repository/item_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/data/repository/record_rows.h"

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
constexpr char NEXT_SEQ_SQL[] =
    "SELECT COALESCE(MAX(v.seq), 0) + 1"
    " FROM item_version v JOIN (SELECT item_id, MAX(cp_seq) AS m FROM item_version WHERE cp_seq <= ?1 GROUP BY item_id) l"
    " ON v.item_id = l.item_id AND v.cp_seq = l.m WHERE v.removed = 0 AND COALESCE(v.parent_id, '') = ?2 AND v.kind = ?3";
constexpr char WRITE_SQL[] =
    "INSERT INTO item_version (item_id, cp_seq, wseq, kind, category, parent_id, seq, title, body, removed)"
    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"
    " ON CONFLICT(item_id, cp_seq) DO UPDATE SET wseq = excluded.wseq, kind = excluded.kind,"
    " category = excluded.category, parent_id = excluded.parent_id, seq = excluded.seq,"
    " title = excluded.title, body = excluded.body, removed = excluded.removed";

void bindOptional(Query &query, int index, const std::optional<std::string> &value)
{
    if (value) {
        query.bind(index, spanOf(*value));
        return;
    }
    query.bind(index, nullptr);
}

void bindParent(Query &query, int index, const ItemRecord &record)
{
    if (record.parentId.empty()) {
        query.bind(index, nullptr);
        return;
    }
    query.bind(index, record.parentId.view());
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
    ItemRecord record;
    record.kind = kind;
    record.category = category;
    const KiottySpecStatus titled = assignTitle(record, title);
    if (titled != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(titled); }
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
    auto record = readItem(_storage, query.value.row());
    if (!record) { return OptionalItemResult::failure(record.code); }
    return OptionalItemResult::success(std::move(record.value));
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
    return collectItems(_storage, prepared.value);
}

KiottySpecStatus ItemStore::update(std::string_view id, const std::optional<std::string> &title,
                                   const std::optional<std::string> &body)
{
    auto record = get(id);
    if (!record) { return record.code; }
    if (title) {
        const KiottySpecStatus titled = assignTitle(record.value, *title);
        if (titled != KIOTTY_SPEC_STATUS_SUCCESS) { return titled; }
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
    const KiottySpecStatus parented = assignParent(record.value, parentId);
    if (parented != KIOTTY_SPEC_STATUS_SUCCESS) { return parented; }
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
    record.id.assign(spanOf(id.value));
    const KiottySpecStatus validated = validateParent(id.value, parentId);
    if (validated != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(validated); }
    const KiottySpecStatus parented = assignParent(record, parentId);
    if (parented != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(parented); }
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
    query.value.bind(2, record.parentId.view());
    query.value.bind(3, spanOf(name(record.kind)));
    const auto stepped = query.value.step();
    if (!stepped) { return SequenceResult::failure(_storage.check(stepped.code)); }
    return SequenceResult::success(query.value.row().integer(0));
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
        cursor = std::string(parent.value->parentId);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemStore::assignTitle(ItemRecord &record, std::string_view title)
{
    const std::string_view trimmed = trim(title);
    if (trimmed.empty()) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("title 이 비어 있음")}); }
    if (record.title.assign(spanOf(trimmed)) == KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("title 이 너무 김 (최대 256 바이트)")});
}

KiottySpecStatus ItemStore::assignParent(ItemRecord &record, const std::optional<std::string> &parentId)
{
    record.parentId.clear();
    if (!parentId) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (record.parentId.assign(spanOf(*parentId)) == KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("상위 항목 없음: "), spanOf(*parentId)});
}

KiottySpecStatus ItemStore::writeVersion(const ItemRecord &record)
{
    const auto writeSeq = _meta.nextWriteSeq();
    if (!writeSeq) { return writeSeq.code; }
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, record.id.view());
    query.value.bind(2, kPending);
    query.value.bind(3, writeSeq.value);
    query.value.bind(4, spanOf(name(record.kind)));
    query.value.bind(5, spanOf(name(record.category)));
    bindParent(query.value, 6, record);
    query.value.bind(7, static_cast<int64_t>(record.seq));
    query.value.bind(8, record.title.view());
    query.value.bind(9, spanOf(record.body));
    query.value.bind(10, static_cast<int64_t>(record.removed ? 1 : 0));
    return _storage.check(query.value.run());
}

}
