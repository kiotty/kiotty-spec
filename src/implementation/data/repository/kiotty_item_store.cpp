#include "implementation/data/repository/kiotty_item_store.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/entity/kiotty_tag_pairs.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::TagRecord;

constexpr char LOAD_SQL[] =
    "SELECT v.item_id, v.checkpoint, v.kind, v.parent_id, v.title, v.summary, v.tags FROM item_version v "
    "JOIN (SELECT item_id, MAX(checkpoint) AS latest FROM item_version GROUP BY item_id) m "
    "ON v.item_id = m.item_id AND v.checkpoint = m.latest WHERE v.removed = 0 ORDER BY v.item_id";

bool lessTag(const TagRecord& left, const TagRecord& right)
{
    if (left.item.index != right.item.index) { return left.item.index < right.item.index; }
    return left.tag < right.tag;
}

}

ItemStore::ItemStore(Storage& storage, size_t max_items, size_t max_tags)
    : _storage(storage), _items(max_items), _id_index(max_items), _tags(max_tags)
{
}

KiottySpecStatus ItemStore::load()
{
    _items.releaseAll();
    _id_index.clear();
    _tag_count = 0;
    auto query = _storage.prepare(literalText(LOAD_SQL));
    if (!query) { return query.code; }
    Result<bool, KiottySpecStatus> stepped = query.value.step();
    while (stepped && stepped.value) {
        const KiottySpecStatus loaded = loadRow(query.value.row());
        if (loaded != KIOTTY_SPEC_STATUS_SUCCESS) { return loaded; }
        stepped = query.value.step();
    }
    if (!stepped) { return _storage.check(stepped.code); }
    std::sort(_tags.begin(), _tags.begin() + static_cast<std::ptrdiff_t>(_tag_count), lessTag);
    return linkParents();
}

KiottySpecStatus ItemStore::loadRow(const Row& row)
{
    ItemRecord record {};
    record.checkpoint = CheckpointHandle{static_cast<uint32_t>(row.integer(1)), 0};
    record.parent = entity::INVALID_ITEM_HANDLE;
    const uint32_t kind = entity::parseKind(viewOf(row.text(2)));
    if (kind == 0) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("알 수 없는 Kind: "), row.text(2)}); }
    record.kind = static_cast<ItemKind>(kind);
    const bool fits = record.id.assign(row.text(0)) == KIOTTY_SPEC_STATUS_SUCCESS && record.title.assign(row.text(4)) == KIOTTY_SPEC_STATUS_SUCCESS &&
                      record.summary.assign(row.text(5)) == KIOTTY_SPEC_STATUS_SUCCESS;
    if (!fits) { return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("항목 필드가 상한을 넘음: "), row.text(0)}); }
    const ItemHandle handle = _items.acquire();
    if (!handle.isValid()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("항목 수가 max_items 를 넘음")}); }
    *_items.get(handle).value = record;
    const KiottySpecStatus indexed = _id_index.insert(record.id, handle.index);
    if (indexed != KIOTTY_SPEC_STATUS_SUCCESS) { return _storage.reject(indexed, {literalText("ID 색인에 넣을 수 없음: "), row.text(0)}); }
    return loadTags(handle, record.kind, row.text(0), row.text(6));
}

// 값이 0 인 태그는 레코드를 두지 않는다.
KiottySpecStatus ItemStore::loadTags(ItemHandle handle, ItemKind kind, Span<const char> id, Span<const char> text)
{
    entity::TagPairs pairs(text);
    Result<bool, KiottySpecStatus> next = pairs.next();
    while (next && next.value) {
        const entity::TagPair pair = pairs.current();
        if (pair.value != 0 && _tag_count == _tags.size()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 를 넘음")}); }
        if (pair.value != 0) { _tags[_tag_count++] = TagRecord{handle, kind, pair.tag, pair.value}; }
        next = pairs.next();
    }
    if (!next) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("태그 형식이 잘못됨: "), id}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// 상위 항목은 모두 적재한 뒤에 핸들로 바꾼다.
KiottySpecStatus ItemStore::linkParents()
{
    auto parents = _storage.prepare(literalText(LOAD_SQL));
    if (!parents) { return parents.code; }
    Result<bool, KiottySpecStatus> stepped = parents.value.step();
    for (; stepped && stepped.value; stepped = parents.value.step()) {
        const Row row = parents.value.row();
        const auto child = row.isNull(3) ? Result<ItemHandle, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND) : reader().find(row.text(0));
        const auto parent = child ? reader().find(row.text(3)) : child;
        if (child && parent) { _items.get(child.value).value->parent = parent.value; }
    }
    return stepped ? KIOTTY_SPEC_STATUS_SUCCESS : _storage.check(stepped.code);
}

}
