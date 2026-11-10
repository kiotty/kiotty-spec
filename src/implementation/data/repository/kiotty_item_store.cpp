#include "implementation/data/repository/kiotty_item_store.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::ItemHandle;
using entity::ItemRecord;
using entity::ItemVersion;
using entity::TagRecord;
using entity::TagVersion;

bool lessTag(const TagRecord& left, const TagRecord& right)
{
    if (left.item.index != right.item.index) { return left.item.index < right.item.index; }
    return left.tag < right.tag;
}

}

void ItemStore::clear()
{
    _t.names.clear();
    _t.versions.clear();
    _t.tag_versions.clear();
}

KiottySpecStatus ItemStore::rebuild()
{
    _t.items.releaseAll();
    _t.id_index.clear();
    _t.key_index.clear();
    _t.tag_count = 0;
    const size_t count = collectLatest();
    const Span<const ItemVersion> versions = _t.versions.all();
    for (size_t position = 0; position < count; ++position) {
        const KiottySpecStatus placed = place(versions[_t.order[position]]);
        if (placed != KIOTTY_SPEC_STATUS_SUCCESS) { return placed; }
    }
    linkParents();
    return loadTags();
}

// 지워지지 않은 key 마다 마지막 버전의 index 를 ID 순으로 order 에 모은다. 풀 index 가 ID 순서를 따르게 한다.
size_t ItemStore::collectLatest()
{
    const Span<const ItemVersion> versions = _t.versions.all();
    size_t count = 0;
    for (size_t index = 0; index < versions.length(); ++index) {
        const bool last = index + 1 == versions.length() || versions[index + 1].key != versions[index].key;
        if (!last || versions[index].removed != 0) { continue; }
        _t.order[count] = static_cast<uint32_t>(index);
        ++count;
    }
    const ItemTables& tables = _t;
    std::sort(_t.order.begin(), _t.order.begin() + static_cast<std::ptrdiff_t>(count), [&tables, versions](uint32_t left, uint32_t right) {
        return tables.names.find(versions[left].key).value < tables.names.find(versions[right].key).value;
    });
    return count;
}

KiottySpecStatus ItemStore::place(const ItemVersion& version)
{
    const auto id = _t.names.find(version.key);
    if (!id) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("item 줄이 없는 항목 키")}); }
    const ItemHandle handle = _t.items.acquire();
    if (!handle.isValid()) { return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("항목 수가 max_items 를 넘음")}); }
    ItemRecord& record = *_t.items.get(handle).value;
    record = ItemRecord{};
    record.checkpoint = CheckpointHandle{version.checkpoint, 0};
    record.parent = entity::INVALID_ITEM_HANDLE;
    record.kind = version.kind;
    record.id = id.value;
    record.title = version.title;
    record.summary = version.summary;
    _t.keys[handle.index] = version.key;
    _t.parents[handle.index] = version.parent;
    _t.key_index.insert(version.key, handle.index);
    const KiottySpecStatus indexed = _t.id_index.insert(record.id, handle.index);
    if (indexed != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("같은 ID 를 가진 항목이 둘: "), record.id.view()}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

void ItemStore::linkParents()
{
    for (size_t index = 0; index < _t.items.capacity(); ++index) {
        const ItemHandle handle = _t.items.handleAt(index);
        if (!handle.isValid() || _t.parents[index] == entity::NO_KEY) { continue; }
        const auto parent = _t.key_index.find(_t.parents[index]);
        if (parent) { _t.items.get(handle).value->parent = _t.items.handleAt(parent.value); }
    }
}

// key 마다 태그의 마지막 버전을 풀에 넣고, 항목의 체크포인트를 태그에 쓴 체크포인트까지 올린다.
KiottySpecStatus ItemStore::loadTags()
{
    const Span<const TagVersion> versions = _t.tag_versions.all();
    for (size_t index = 0; index < versions.length(); ++index) {
        const TagVersion& tag = versions[index];
        const bool last = index + 1 == versions.length() || !(versions[index + 1].identity() == tag.identity());
        const auto position = last ? _t.key_index.find(tag.key) : Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
        if (!position) { continue; }
        const ItemHandle handle = _t.items.handleAt(position.value);
        ItemRecord& record = *_t.items.get(handle).value;
        if (tag.checkpoint > record.checkpoint.sequence) { record.checkpoint = CheckpointHandle{tag.checkpoint, 0}; }
        if (tag.removed != 0 || tag.value == 0) { continue; }
        if (_t.tag_count == _t.tags.size()) { return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("태그 수가 max_tags 를 넘음")}); }
        _t.tags[_t.tag_count] = TagRecord{handle, record.kind, tag.tag, tag.value};
        ++_t.tag_count;
    }
    std::sort(_t.tags.begin(), _t.tags.begin() + static_cast<std::ptrdiff_t>(_t.tag_count), lessTag);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
