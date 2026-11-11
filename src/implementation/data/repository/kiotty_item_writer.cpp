#include "implementation/data/repository/kiotty_item_writer.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/kiotty_item_history.h"

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

constexpr uint32_t MAX_KEY_ATTEMPTS = 64;
constexpr uint32_t KEY_SHIFT = 32;

}

Result<ItemHandle, KiottySpecStatus> ItemWriter::create(const ItemRecord& record, uint32_t sequence)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    const KiottySpecStatus unused = checkUnused(record.id);
    if (unused != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(unused); }
    if (_t.id_index.length() == _t.id_index.capacity()) {
        return HandleResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items")));
    }
    const auto key = newKey(record.id);
    if (!key) { return HandleResult::failure(key.code); }
    ItemVersion version {};
    version.key = key.value;
    version.checkpoint = sequence;
    version.parent = reader().key(record.parent);
    version.kind = record.kind;
    version.title = record.title;
    version.summary = record.summary;
    KiottySpecStatus status = _t.names.insert(key.value, record.id);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _t.versions.write(version); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(_failure.raise(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items"))); }
    const ItemHandle handle = _t.items.acquire();
    if (!handle.isValid()) { return HandleResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    ItemRecord& stored = *_t.items.get(handle).value;
    stored = record;
    stored.checkpoint = CheckpointHandle{sequence, 0};
    _t.keys[handle.index] = key.value;
    _t.parents[handle.index] = version.parent;
    _t.id_index.insert(stored.id, handle.index);
    _t.key_index.insert(key.value, handle.index);
    return HandleResult::success(handle);
}

KiottySpecStatus ItemWriter::checkUnused(IdText id)
{
    if (_t.id_index.find(id)) { return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID, id.view()); }
    const auto removed = ItemHistory(_t).keyOf(id.view());
    if (removed) { return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY, id.view()); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<uint32_t, KiottySpecStatus> ItemWriter::newKey(IdText id) const
{
    const uint64_t moment = static_cast<uint64_t>(nowEpochSeconds()) ^ (static_cast<uint64_t>(_t.names.length()) << KEY_SHIFT);
    for (uint32_t attempt = 0; attempt < MAX_KEY_ATTEMPTS; ++attempt) {
        const uint64_t mixed = mix64(hash64(id.view(), moment + attempt));
        const uint32_t key = static_cast<uint32_t>(mixed ^ (mixed >> KEY_SHIFT));
        if (key != entity::NO_KEY && !_t.names.find(key)) { return Result<uint32_t, KiottySpecStatus>::success(key); }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(_failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_ID, id.view()));
}

Result<ItemVersion, KiottySpecStatus> ItemWriter::currentVersion(ItemHandle handle) const
{
    if (!_t.items.get(handle)) { return Result<ItemVersion, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE); }
    const ItemVersion* version = _t.versions.latest(_t.keys[handle.index], UINT32_MAX);
    if (version == nullptr) { return Result<ItemVersion, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return Result<ItemVersion, KiottySpecStatus>::success(*version);
}

KiottySpecStatus ItemWriter::writeVersion(ItemHandle handle, const ItemVersion& version)
{
    const KiottySpecStatus written = _t.versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_items")); }
    _t.items.get(handle).value->checkpoint = CheckpointHandle{version.checkpoint, 0};
    return KIOTTY_SPEC_STATUS_SUCCESS;
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
    ItemRecord& stored = *_t.items.get(handle).value;
    stored.title = record.title;
    stored.summary = record.summary;
    stored.parent = record.parent;
    _t.parents[handle.index] = version.value.parent;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemWriter::setTag(ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence)
{
    const auto stored = _t.items.get(handle);
    if (!stored) { return stored.code; }
    const bool adds = value != 0 && tags().value(handle, tag) == 0;
    if (adds && _t.tag_count == _t.tags.size()) { return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_tags")); }
    const TagVersion version {value, _t.keys[handle.index], tag, sequence, value == 0 ? 1u : 0u};
    const KiottySpecStatus written = _t.tag_versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_tags")); }
    stored.value->checkpoint = CheckpointHandle{sequence, 0};
    return applyTag(handle, stored.value->kind, tag, value);
}

KiottySpecStatus ItemWriter::applyTag(ItemHandle handle, ItemKind kind, uint32_t tag, uint64_t value)
{
    const size_t position = tags().position(handle.index, tag);
    const bool exists = position < _t.tag_count && _t.tags[position].item.index == handle.index && _t.tags[position].tag == tag;
    if (exists && value != 0) {
        _t.tags[position].value = value;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    if (exists) {
        for (size_t index = position + 1; index < _t.tag_count; ++index) { _t.tags[index - 1] = _t.tags[index]; }
        --_t.tag_count;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    if (value == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    for (size_t index = _t.tag_count; index > position; --index) { _t.tags[index] = _t.tags[index - 1]; }
    _t.tags[position] = TagRecord{handle, kind, tag, value};
    ++_t.tag_count;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ItemWriter::writeBody(ItemHandle handle, Span<const char> body, uint32_t sequence)
{
    auto version = currentVersion(handle);
    if (!version) { return version.code; }
    const KiottySpecStatus stored = _t.bodies.put(version.value.key, body);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(stored, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_body_bytes")); }
    version.value.checkpoint = sequence;
    version.value.body_hash = body.length() == 0 ? 0 : hash64(body);
    return writeVersion(handle, version.value);
}

void ItemWriter::dropTags(ItemHandle handle)
{
    size_t kept = 0;
    for (size_t index = 0; index < _t.tag_count; ++index) {
        if (_t.tags[index].item.index == handle.index) { continue; }
        _t.tags[kept] = _t.tags[index];
        ++kept;
    }
    _t.tag_count = kept;
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
    _t.id_index.erase(_t.items.get(handle).value->id);
    _t.key_index.erase(version.value.key);
    _t.keys[handle.index] = entity::NO_KEY;
    return _t.items.release(handle);
}

KiottySpecStatus ItemWriter::rename(ItemHandle handle, IdText id)
{
    const auto stored = _t.items.get(handle);
    if (!stored) { return stored.code; }
    const KiottySpecStatus unused = checkUnused(id);
    if (unused != KIOTTY_SPEC_STATUS_SUCCESS) { return unused; }
    const uint32_t key = _t.keys[handle.index];
    _t.names.erase(key);
    _t.names.insert(key, id);
    _t.id_index.erase(stored.value->id);
    stored.value->id = id;
    return _t.id_index.insert(id, handle.index);
}

}
