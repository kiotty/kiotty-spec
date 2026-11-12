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
using entity::ItemRecord;
using entity::ItemVersion;
using entity::TagRecord;
using entity::TagVersion;

using CountResult = Result<size_t, KiottySpecStatus>;

constexpr uint32_t MAX_KEY_ATTEMPTS = 64;
constexpr uint32_t KEY_SHIFT = 32;

ItemVersion buildVersion(const ItemRecord& record, uint32_t key, uint32_t sequence,
                         uint32_t parent);
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

Result<ItemHandle, KiottySpecStatus> ItemWriter::create(const ItemRecord& record, uint32_t sequence)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    const KiottySpecStatus unused = checkUnused(record.id);
    if (unused != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(unused); }
    const bool is_full = isAtCapacity(_tables.id_index.length(), _tables.items.capacity());
    if (is_full) {
        return HandleResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL,
                                                    KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                                    literalText("max_items")));
    }
    const auto key = newKey(record.id);
    if (!key) { return HandleResult::failure(key.code); }
    const uint32_t parent = reader().key(record.parent);
    const ItemVersion version = buildVersion(record, key.value, sequence, parent);
    KiottySpecStatus status = _tables.names.insert(key.value, record.id);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _tables.versions.write(version); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return HandleResult::failure(
            _failure.raise(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items")));
    }
    const ItemHandle handle = _tables.items.acquire();
    const bool is_valid = handle.isValid();
    if (!is_valid) { return HandleResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    ItemRecord& stored = *_tables.items.get(handle).value;
    stored = record;
    stored.checkpoint = CheckpointHandle{sequence, 0};
    status = writeLinks(viewKeys(), viewParents(), handle.index, version);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(status); }
    _tables.id_index.insert(stored.id, handle.index);
    _tables.key_index.insert(key.value, handle.index);
    return HandleResult::success(handle);
}

KiottySpecStatus ItemWriter::update(ItemHandle handle, const ItemRecord& record, uint32_t sequence)
{
    auto version = currentVersion(handle);
    if (!version) { return version.code; }
    version.value.checkpoint = sequence;
    version.value.title = record.title;
    version.value.summary = record.summary;
    version.value.parent = reader().key(record.parent);
    const KiottySpecStatus written = writeVersion(handle, version.value);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    ItemRecord& stored = *_tables.items.get(handle).value;
    stored.title = record.title;
    stored.summary = record.summary;
    stored.parent = record.parent;
    const Span<uint32_t> parents = viewParents();
    return writeAt(parents, handle.index, version.value.parent);
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
    const TagVersion version {value, key, tag, sequence, value == 0 ? 1u : 0u};
    const KiottySpecStatus written = _tables.tag_versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_tags"));
    }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return applyTag(handle, stored.value->kind, tag, value);
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
    _tables.id_index.erase(_tables.items.get(handle).value->id);
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
    _tables.id_index.erase(stored.value->id);
    stored.value->id = id;
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
    const ItemVersion* version = _tables.versions.latest(key, UINT32_MAX);
    if (version == nullptr) {
        return Result<ItemVersion, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<ItemVersion, KiottySpecStatus>::success(*version);
}

KiottySpecStatus ItemWriter::writeVersion(ItemHandle handle, const ItemVersion& version)
{
    const KiottySpecStatus written = _tables.versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items"));
    }
    _tables.items.get(handle).value->checkpoint = CheckpointHandle{version.checkpoint, 0};
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemWriter::applyTag(ItemHandle handle, ItemKind kind, uint32_t tag,
                                      uint64_t value)
{
    const size_t position = tags().position(handle.index, tag);
    const Span<TagRecord> records = viewTags();
    const TagRecord record {handle, kind, tag, value};
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

ItemVersion buildVersion(const ItemRecord& record, uint32_t key, uint32_t sequence,
                         uint32_t parent)
{
    ItemVersion version {};
    version.key = key;
    version.checkpoint = sequence;
    version.parent = parent;
    version.kind = record.kind;
    version.title = record.title;
    version.summary = record.summary;
    return version;
}

KiottySpecStatus writeLinks(Span<uint32_t> keys, Span<uint32_t> parents, uint32_t index,
                            const ItemVersion& version)
{
    const KiottySpecStatus key_written = writeAt(keys, index, version.key);
    if (key_written != KIOTTY_SPEC_STATUS_SUCCESS) { return key_written; }
    return writeAt(parents, index, version.parent);
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
