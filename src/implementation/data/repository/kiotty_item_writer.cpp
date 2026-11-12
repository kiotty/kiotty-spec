#include "implementation/data/repository/kiotty_item_writer.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/kiotty_item_history.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemLabel;
using entity::ItemState;
using entity::ItemVersion;
using entity::TagRecord;
using entity::TagVersion;

using CountResult = Result<size_t, KiottySpecStatus>;

constexpr uint32_t MAX_KEY_ATTEMPTS = 64;
constexpr uint32_t KEY_SHIFT = 32;

ItemVersion buildVersion(entity::ItemKind kind, uint32_t key, uint32_t sequence, uint32_t parent);
KiottySpecStatus writeLinks(Span<uint32_t> keys, Span<uint32_t> parents, uint32_t index,
                            const ItemVersion& version);
KiottySpecStatus writeAt(Span<uint32_t> values, uint32_t index, uint32_t value);
bool isAtCapacity(size_t count, size_t capacity);
CountResult applyRecord(Span<TagRecord> records, size_t count, size_t position,
                        const TagRecord& record);
bool isSameTag(const TagRecord& left, const TagRecord& right);
size_t removeAt(Span<TagRecord> used, size_t position);
CountResult insertAt(Span<TagRecord> records, size_t count, size_t position,
                     const TagRecord& record);
size_t removeItemTags(Span<TagRecord> used, uint32_t index);

}

Result<ItemHandle, KiottySpecStatus> ItemWriter::create(const ItemLabel& label,
                                                        const ItemState& state, uint32_t sequence)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    const KiottySpecStatus unused = checkUnused(label.id);
    if (unused != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(unused); }
    const bool is_full = isAtCapacity(_tables.id_index.length(), _tables.items.capacity());
    if (is_full) {
        return HandleResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL,
                                                    KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                                    literalText("max_items")));
    }
    const Result<ItemVersion, KiottySpecStatus> version = recordVersion(label, state, sequence);
    if (!version) { return HandleResult::failure(version.code); }
    return storeItem(label, state, version.value);
}

Result<ItemVersion, KiottySpecStatus>
ItemWriter::recordVersion(const ItemLabel& label, const ItemState& state, uint32_t sequence)
{
    using VersionResult = Result<ItemVersion, KiottySpecStatus>;
    const Result<uint32_t, KiottySpecStatus> key = newKey(label.id);
    if (!key) { return VersionResult::failure(key.code); }
    const Result<entity::ItemLabelHandle, KiottySpecStatus> version_label = storeLabel(label);
    if (!version_label) { return VersionResult::failure(version_label.code); }
    const uint32_t parent = reader().key(state.parent);
    ItemVersion version = buildVersion(state.kind, key.value, sequence, parent);
    version.label = version_label.value;
    KiottySpecStatus status = _tables.names.insert(key.value, label.id);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _tables.versions.write(version); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return VersionResult::failure(
            _failure.raise(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items")));
    }
    return VersionResult::success(version);
}

Result<ItemHandle, KiottySpecStatus> ItemWriter::storeItem(const ItemLabel& label,
                                                           const ItemState& state,
                                                           const ItemVersion& version)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    const ItemHandle handle = _tables.items.acquire();
    const Result<ItemLabel&, KiottySpecStatus> stored_label = labelOf(handle);
    if (!stored_label) { return HandleResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    *stored_label.value = label;
    ItemState& stored = *_tables.items.get(handle).value;
    stored = state;
    stored.checkpoint = CheckpointHandle{version.checkpoint, 0};
    const KiottySpecStatus linked = writeLinks(viewKeys(), viewParents(), handle.index, version);
    if (linked != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(linked); }
    _tables.id_index.insert(label.id, handle.index);
    _tables.key_index.insert(version.key, handle.index);
    return HandleResult::success(handle);
}

KiottySpecStatus ItemWriter::update(ItemHandle handle, const ItemLabel& label, ItemHandle parent,
                                    uint32_t sequence)
{
    auto version = currentVersion(handle);
    if (!version) { return version.code; }
    const auto version_label = storeLabel(label);
    if (!version_label) { return version_label.code; }
    version.value.checkpoint = sequence;
    version.value.label = version_label.value;
    version.value.parent = entity::makeItemKey(reader().key(parent));
    const KiottySpecStatus written = writeVersion(handle, version.value);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    const Result<ItemLabel&, KiottySpecStatus> stored = labelOf(handle);
    if (!stored) { return stored.code; }
    (*stored.value).title = label.title;
    (*stored.value).summary = label.summary;
    (*_tables.items.get(handle).value).parent = parent;
    const Span<uint32_t> parents = viewParents();
    return writeAt(parents, handle.index, version.value.parent.index);
}

KiottySpecStatus ItemWriter::setTag(ItemHandle handle, uint32_t tag, uint64_t value,
                                    uint32_t sequence)
{
    const auto stored = _tables.items.get(handle);
    if (!stored) { return stored.code; }
    const bool is_adding = value != 0 && tags().value(handle, tag) == 0;
    const bool is_full = isAtCapacity(_tables.tag_count, _tables.tags.size());
    if (is_adding && is_full) {
        return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_tags"));
    }
    const uint32_t key = reader().key(handle);
    const uint32_t removed = value == 0 ? 1u : 0u;
    const TagVersion version {value, entity::makeItemKey(key), tag, sequence, removed};
    const KiottySpecStatus written = _tables.tag_versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_tags"));
    }
    ItemState& state = *stored.value;
    state.checkpoint = CheckpointHandle{sequence, 0};
    return applyTag(handle, state.kind, tag, value);
}

KiottySpecStatus ItemWriter::writeBody(ItemHandle handle, Span<const char> body, uint32_t sequence)
{
    auto version = currentVersion(handle);
    if (!version) { return version.code; }
    const KiottySpecStatus stored = _tables.bodies.put(version.value.key, body);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(stored, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_body_bytes"));
    }
    version.value.checkpoint = sequence;
    version.value.body_hash = body.length() == 0 ? 0 : hash64(body);
    return writeVersion(handle, version.value);
}

KiottySpecStatus ItemWriter::remove(ItemHandle handle, uint32_t sequence)
{
    auto version = currentVersion(handle);
    if (!version) { return version.code; }
    version.value.checkpoint = sequence;
    version.value.removed = 1;
    const KiottySpecStatus written = writeVersion(handle, version.value);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    dropTags(handle);
    const Result<ItemLabel&, KiottySpecStatus> label = labelOf(handle);
    if (label) { _tables.id_index.erase((*label.value).id); }
    _tables.key_index.erase(version.value.key);
    const Span<uint32_t> keys = viewKeys();
    const KiottySpecStatus cleared = writeAt(keys, handle.index, entity::NO_KEY);
    if (cleared != KIOTTY_SPEC_STATUS_SUCCESS) { return cleared; }
    return _tables.items.release(handle);
}

KiottySpecStatus ItemWriter::rename(ItemHandle handle, IdText id)
{
    const auto stored = _tables.items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus unused = checkUnused(id);
    if (unused != KIOTTY_SPEC_STATUS_SUCCESS) { return unused; }
    const uint32_t key = reader().key(handle);
    _tables.names.erase(key);
    _tables.names.insert(key, id);
    const Result<ItemLabel&, KiottySpecStatus> label = labelOf(handle);
    if (!label) { return label.code; }
    _tables.id_index.erase((*label.value).id);
    (*label.value).id = id;
    return _tables.id_index.insert(id, handle.index);
}

KiottySpecStatus ItemWriter::checkUnused(IdText id)
{
    const auto existing = _tables.id_index.find(id);
    if (existing) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID, id.view());
    }
    const auto removed = ItemHistory(_tables).keyOf(id.view());
    if (removed) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY,
                              id.view());
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<uint32_t, KiottySpecStatus> ItemWriter::newKey(IdText id) const
{
    const uint64_t moment = static_cast<uint64_t>(nowEpochSeconds()) ^
                            (static_cast<uint64_t>(_tables.names.length()) << KEY_SHIFT);
    for (uint32_t attempt = 0; attempt < MAX_KEY_ATTEMPTS; ++attempt) {
        const uint64_t mixed = mix64(hash64(id.view(), moment + attempt));
        const uint32_t key = static_cast<uint32_t>(mixed ^ (mixed >> KEY_SHIFT));
        if (key == entity::NO_KEY) { continue; }
        const auto existing = _tables.names.find(key);
        if (!existing) { return Result<uint32_t, KiottySpecStatus>::success(key); }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(
        _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID, id.view()));
}

Result<ItemVersion, KiottySpecStatus> ItemWriter::currentVersion(ItemHandle handle) const
{
    const auto record = _tables.items.get(handle);
    if (!record) {
        return Result<ItemVersion, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE);
    }
    const uint32_t key = reader().key(handle);
    const auto version = _tables.versions.latest(key, UINT32_MAX);
    if (!version) {
        return Result<ItemVersion, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemVersion, KiottySpecStatus>::success(*version.value);
}

KiottySpecStatus ItemWriter::writeVersion(ItemHandle handle, const ItemVersion& version)
{
    const KiottySpecStatus written = _tables.versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items"));
    }
    ItemState& state = *_tables.items.get(handle).value;
    state.checkpoint = CheckpointHandle{version.checkpoint, 0};
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<entity::ItemLabelHandle, KiottySpecStatus> ItemWriter::storeLabel(const ItemLabel& label)
{
    using LabelResult = Result<entity::ItemLabelHandle, KiottySpecStatus>;
    const LabelResult handle =
        _tables.version_labels.acquire(_tables.versions.all(), pickLabelHandle);
    if (!handle) {
        return LabelResult::failure(_failure.raise(handle.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                                   literalText("max_items")));
    }
    entity::ItemVersionLabel& stored = *_tables.version_labels.get(handle.value).value;
    stored.title = label.title;
    stored.summary = label.summary;
    return handle;
}

KiottySpecStatus ItemWriter::applyTag(ItemHandle handle, ItemKind kind, uint32_t tag,
                                      uint64_t value)
{
    const size_t position = tags().position(handle.index, tag);
    const Span<TagRecord> records = viewTags();
    const TagRecord record {value, handle, kind, tag};
    const CountResult applied = applyRecord(records, _tables.tag_count, position, record);
    if (!applied) { return applied.code; }
    _tables.tag_count = applied.value;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

void ItemWriter::dropTags(ItemHandle handle)
{
    const Span<TagRecord> used(viewTags().begin(), _tables.tag_count);
    _tables.tag_count = removeItemTags(used, handle.index);
}

namespace {

ItemVersion buildVersion(entity::ItemKind kind, uint32_t key, uint32_t sequence, uint32_t parent)
{
    ItemVersion version {};
    version.key = key;
    version.checkpoint = sequence;
    version.parent = entity::makeItemKey(parent);
    version.kind = kind;
    return version;
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

bool isAtCapacity(size_t count, size_t capacity) { return count == capacity; }

CountResult applyRecord(Span<TagRecord> records, size_t count, size_t position,
                        const TagRecord& record)
{
    const Span<TagRecord> used(records.begin(), count);
    const Result<TagRecord&, KiottySpecStatus> found = used.at(position);
    const bool is_existing = found && isSameTag(*found.value, record);
    if (is_existing && record.value != 0) {
        TagRecord& existing = *found.value;
        existing.value = record.value;
        return CountResult::success(count);
    }
    if (is_existing) { return CountResult::success(removeAt(used, position)); }
    if (record.value == 0) { return CountResult::success(count); }
    return insertAt(records, count, position, record);
}

bool isSameTag(const TagRecord& left, const TagRecord& right)
{ return left.item.index == right.item.index && left.tag == right.tag; }

size_t removeAt(Span<TagRecord> used, size_t position)
{
    std::copy(used.begin() + position + 1, used.end(), used.begin() + position);
    return used.length() - 1;
}

CountResult insertAt(Span<TagRecord> records, size_t count, size_t position,
                     const TagRecord& record)
{
    const Result<TagRecord&, KiottySpecStatus> spare = records.at(count);
    if (!spare) { return CountResult::failure(spare.code); }
    std::copy_backward(records.begin() + position, records.begin() + count,
                       records.begin() + count + 1);
    const Result<TagRecord&, KiottySpecStatus> slot = records.at(position);
    if (!slot) { return CountResult::failure(slot.code); }
    *slot.value = record;
    return CountResult::success(count + 1);
}

size_t removeItemTags(Span<TagRecord> used, uint32_t index)
{
    return static_cast<size_t>(
        std::remove_if(used.begin(), used.end(),
                       [index](const TagRecord& record) { return record.item.index == index; }) -
        used.begin());
}

}

}
