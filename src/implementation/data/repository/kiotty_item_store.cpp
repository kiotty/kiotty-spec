#include "implementation/data/repository/kiotty_item_store.h"

#include "implementation/core/kiotty_text_view.h"

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

// "[[1,4],[2,1]]" 을 읽는다. 숫자 두 개씩 묶는다. 형식이 어긋나면 false
template <typename Visit>
bool parseTagText(Span<const char> text, Visit visit)
{
    uint64_t numbers[2] {};
    size_t count = 0;
    bool in_number = false;
    uint64_t current = 0;
    for (const char letter : text) {
        if (letter >= '0' && letter <= '9') {
            current = current * 10 + static_cast<uint64_t>(letter - '0');
            in_number = true;
            continue;
        }
        if (in_number) {
            if (count == 2) { return false; }
            numbers[count++] = current;
            current = 0;
            in_number = false;
        }
        if (letter == ']' && count == 2) {
            if (!visit(static_cast<uint32_t>(numbers[0]), numbers[1])) { return false; }
            count = 0;
        }
    }
    return count == 0 && !in_number;
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
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = query.value.row();
        ItemRecord record {};
        record.checkpoint = CheckpointHandle{static_cast<uint32_t>(row.integer(1)), 0};
        record.parent = entity::INVALID_ITEM_HANDLE;
        const uint32_t kind = entity::parseKind(viewOf(row.text(2)));
        if (kind == 0) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("알 수 없는 Kind: "), row.text(2)}); }
        record.kind = static_cast<ItemKind>(kind);
        if (record.id.assign(row.text(0)) != KIOTTY_SPEC_STATUS_SUCCESS || record.title.assign(row.text(4)) != KIOTTY_SPEC_STATUS_SUCCESS ||
            record.summary.assign(row.text(5)) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("항목 필드가 상한을 넘음: "), row.text(0)});
        }
        const ItemHandle handle = _items.acquire();
        if (!handle.isValid()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("항목 수가 max_items 를 넘음")}); }
        *_items.get(handle).value = record;
        const KiottySpecStatus indexed = _id_index.insert(record.id, handle.index);
        if (indexed != KIOTTY_SPEC_STATUS_SUCCESS) { return _storage.reject(indexed, {literalText("ID 색인에 넣을 수 없음: "), row.text(0)}); }
        bool fits = true;
        const bool parsed = parseTagText(row.text(6), [&](uint32_t tag, uint64_t value) {
            if (value == 0) { return true; }
            if (_tag_count == _tags.size()) {
                fits = false;
                return false;
            }
            _tags[_tag_count++] = TagRecord{handle, record.kind, tag, value};
            return true;
        });
        if (!fits) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 를 넘음")}); }
        if (!parsed) { return _storage.reject(KIOTTY_SPEC_STATUS_DATABASE, {literalText("태그 형식이 잘못됨: "), row.text(0)}); }
    }
    std::sort(_tags.begin(), _tags.begin() + static_cast<std::ptrdiff_t>(_tag_count), lessTag);

    // 상위 항목은 모두 적재한 뒤에 핸들로 바꾼다.
    auto parents = _storage.prepare(literalText(LOAD_SQL));
    if (!parents) { return parents.code; }
    while (true) {
        const auto stepped = parents.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = parents.value.row();
        if (row.isNull(3)) { continue; }
        const auto child = reader().find(row.text(0));
        const auto parent = reader().find(row.text(3));
        if (!child || !parent) { continue; }
        _items.get(child.value).value->parent = parent.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
