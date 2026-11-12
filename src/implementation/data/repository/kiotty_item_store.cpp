#include "implementation/data/repository/kiotty_item_store.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::IdText;
using entity::ItemHandle;
using entity::ItemLabel;
using entity::ItemState;
using entity::ItemVersion;
using entity::TagRecord;
using entity::TagVersion;

size_t collectLatestItems(Span<uint32_t> order, Span<const ItemVersion> versions);
bool isLastVersion(Span<const ItemVersion> versions, size_t next_index, uint32_t key);
bool isLastTag(Span<const TagVersion> versions, size_t next_index, const TagVersion& tag);
void sortById(Span<uint32_t> latest, Span<const ItemVersion> versions, const ItemTables& tables);
uint32_t readItemKey(Span<const ItemVersion> versions, uint32_t index);
ItemState buildState(const ItemVersion& version);
ItemLabel buildLabel(const entity::ItemVersionLabel& version_label, IdText id);
KiottySpecStatus writeLinks(Span<uint32_t> keys, Span<uint32_t> parents, uint32_t index,
                            const ItemVersion& version);
KiottySpecStatus writeAt(Span<uint32_t> values, uint32_t index, uint32_t value);
bool isTagBefore(const TagRecord& left, const TagRecord& right);
bool hasVersion(const ItemTables& tables, uint32_t key);

}

void ItemMaintenance::clear()
{
    _tables.names.clear();
    _tables.versions.batch().clear();
    _tables.tag_versions.batch().clear();
    _tables.bodies.clear();
}

KiottySpecStatus ItemMaintenance::rebuild()
{
    _tables.items.releaseAll();
    _tables.id_index.clear();
    _tables.key_index.clear();
    _tables.tag_count = 0;
    const Span<const uint32_t> order = collectLatest();
    const Span<const ItemVersion> versions = _tables.versions.all();
    for (const uint32_t index : order) {
        const Result<const ItemVersion&, KiottySpecStatus> version = versions.at(index);
        if (!version) { return version.code; }
        const KiottySpecStatus placed = place(*version.value);
        if (placed != KIOTTY_SPEC_STATUS_SUCCESS) { return placed; }
    }
    linkParents();
    return loadTags();
}

Span<const uint32_t> ItemMaintenance::collectLatest()
{
    const Span<const ItemVersion> versions = _tables.versions.all();
    const Span<uint32_t> order = viewVector(_tables.order);
    const Span<uint32_t> latest(order.begin(), collectLatestItems(order, versions));
    sortById(latest, versions, _tables);
    return latest;
}

KiottySpecStatus ItemMaintenance::place(const ItemVersion& version)
{
    const auto id = _tables.names.find(version.key);
    if (!id) {
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE_LINE,
                              literalText("item_version"));
    }
    const ItemHandle handle = _tables.items.acquire();
    const bool is_valid = handle.isValid();
    if (!is_valid) {
        return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_items"));
    }
    *_tables.items.get(handle).value = buildState(version);
    const Result<ItemLabel&, KiottySpecStatus> slot = viewVector(_tables.labels).at(handle.index);
    if (!slot) { return slot.code; }
    ItemLabel& label = *slot.value;
    label = buildLabel(_tables.version_labels.read(version.label), id.value);
    const Span<uint32_t> keys = viewVector(_tables.keys);
    const Span<uint32_t> parents = viewVector(_tables.parents);
    const KiottySpecStatus linked = writeLinks(keys, parents, handle.index, version);
    if (linked != KIOTTY_SPEC_STATUS_SUCCESS) { return linked; }
    _tables.key_index.insert(version.key, handle.index);
    const KiottySpecStatus indexed = _tables.id_index.insert(label.id, handle.index);
    if (indexed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE,
                              label.id.view());
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

void ItemMaintenance::linkParents()
{
    const size_t capacity = _tables.items.capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _tables.items.handleAt(index);
        const bool is_valid = handle.isValid();
        const uint32_t parent_key = _tables.parents[index];
        const bool is_skipped = !is_valid || parent_key == entity::NO_KEY;
        if (is_skipped) { continue; }
        const auto parent = _tables.key_index.find(parent_key);
        if (!parent) { continue; }
        ItemState& state = *_tables.items.get(handle).value;
        state.parent = _tables.items.handleAt(parent.value);
    }
}

KiottySpecStatus ItemMaintenance::loadTags()
{
    const Span<const TagVersion> versions = _tables.tag_versions.all();
    const size_t length = versions.length();
    const Span<TagRecord> tags = viewVector(_tables.tags);
    for (size_t index = 0; index < length; ++index) {
        const TagVersion& tag = versions[index];
        const bool is_last = isLastTag(versions, index + 1, tag);
        const auto position =
            is_last ? _tables.key_index.find(tag.item.index)
                    : Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
        if (!position) { continue; }
        const ItemHandle handle = _tables.items.handleAt(position.value);
        ItemState& state = *_tables.items.get(handle).value;
        if (tag.checkpoint > state.checkpoint.sequence) {
            state.checkpoint = CheckpointHandle { tag.checkpoint, 0 };
        }
        if (tag.removed != 0 || tag.value == 0) { continue; }
        const Result<TagRecord&, KiottySpecStatus> slot = tags.at(_tables.tag_count);
        if (!slot) {
            return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL,
                                  KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_tags"));
        }
        *slot.value = TagRecord{tag.value, handle, state.kind, tag.tag};
        ++_tables.tag_count;
    }
    const Span<TagRecord> loaded(tags.begin(), _tables.tag_count);
    std::sort(loaded.begin(), loaded.end(), isTagBefore);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}


void ItemMaintenance::prune(uint32_t base)
{
    _tables.versions.batch().prune(base);
    const ItemTables& tables = _tables;
    _tables.tag_versions.batch().retain(
        [&tables](const TagVersion& tag) { return hasVersion(tables, tag.item.index); });
    _tables.tag_versions.batch().prune(base);
    for (size_t index = _tables.names.length(); index > 0; --index) {
        const uint32_t key = _tables.names.keyAt(index - 1);
        const bool has_version = hasVersion(_tables, key);
        if (has_version) { continue; }
        _tables.bodies.erase(key);
        _tables.names.erase(key);
    }
}

namespace {

size_t collectLatestItems(Span<uint32_t> order, Span<const ItemVersion> versions)
{
    size_t count = 0;
    const size_t length = versions.length();
    for (size_t index = 0; index < length; ++index) {
        const ItemVersion& version = versions[index];
        const bool is_last = isLastVersion(versions, index + 1, version.key);
        const bool is_kept = is_last && version.removed == 0;
        const Result<uint32_t&, KiottySpecStatus> slot = order.at(count);
        if (is_kept && slot) {
            *slot.value = static_cast<uint32_t>(index);
            ++count;
        }
    }
    return count;
}

bool isLastVersion(Span<const ItemVersion> versions, size_t next_index, uint32_t key)
{
    const Result<const ItemVersion&, KiottySpecStatus> next = versions.at(next_index);
    if (!next) { return true; }
    const ItemVersion& following = *next.value;
    return following.key != key;
}

bool isLastTag(Span<const TagVersion> versions, size_t next_index, const TagVersion& tag)
{
    const Result<const TagVersion&, KiottySpecStatus> next = versions.at(next_index);
    if (!next) { return true; }
    const TagVersion& following = *next.value;
    return !(following.identity() == tag.identity());
}

void sortById(Span<uint32_t> latest, Span<const ItemVersion> versions, const ItemTables& tables)
{
    std::sort(latest.begin(), latest.end(), [&tables, versions](uint32_t left, uint32_t right) {
        return tables.names.find(readItemKey(versions, left)).value <
               tables.names.find(readItemKey(versions, right)).value;
    });
}

uint32_t readItemKey(Span<const ItemVersion> versions, uint32_t index)
{
    const Result<const ItemVersion&, KiottySpecStatus> version = versions.at(index);
    return version ? (*version.value).key : entity::NO_KEY;
}

ItemState buildState(const ItemVersion& version)
{
    ItemState state {};
    state.checkpoint = CheckpointHandle{version.checkpoint, 0};
    state.parent = entity::INVALID_ITEM_HANDLE;
    state.kind = version.kind;
    return state;
}

ItemLabel buildLabel(const entity::ItemVersionLabel& version_label, IdText id)
{
    ItemLabel label {};
    label.id = id;
    label.title = version_label.title;
    label.summary = version_label.summary;
    return label;
}

KiottySpecStatus writeLinks(Span<uint32_t> keys, Span<uint32_t> parents, uint32_t index,
                            const ItemVersion& version)
{
    const KiottySpecStatus key_written = writeAt(keys, index, version.key);
    if (key_written != KIOTTY_SPEC_STATUS_SUCCESS) { return key_written; }
    return writeAt(parents, index, version.parent.index);
}

KiottySpecStatus writeAt(Span<uint32_t> values, uint32_t index, uint32_t value)
{
    const Result<uint32_t&, KiottySpecStatus> slot = values.at(index);
    if (!slot) { return slot.code; }
    *slot.value = value;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

bool isTagBefore(const TagRecord& left, const TagRecord& right)
{
    if (left.item.index != right.item.index) { return left.item.index < right.item.index; }
    return left.tag < right.tag;
}

bool hasVersion(const ItemTables& tables, uint32_t key)
{
    const auto version = tables.versions.latest(key, UINT32_MAX);
    return static_cast<bool>(version);
}

}

}
