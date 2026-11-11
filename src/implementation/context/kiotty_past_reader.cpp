#include "implementation/context/kiotty_past_reader.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemVersion;

}

Result<const ItemVersion*, KiottySpecStatus> PastReader::versionAt(Span<const char> id,
                                                                   uint32_t checkpoint)
{
    using VersionResult = Result<const ItemVersion*, KiottySpecStatus>;
    const auto key = _w.items.history().keyOf(id);
    const ItemVersion* version =
        key ? _w.items.tables().versions.latest(key.value, checkpoint) : nullptr;
    if (version == nullptr || version->removed != 0) {
        return VersionResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id));
    }
    return VersionResult::success(version);
}

Result<ItemVersionView, KiottySpecStatus> PastReader::itemAt(Span<const char> id,
                                                             uint32_t checkpoint)
{
    using ViewResult = Result<ItemVersionView, KiottySpecStatus>;
    const auto version = versionAt(id, checkpoint);
    if (!version) { return ViewResult::failure(version.code); }
    const ItemVersion& found = *version.value;
    ItemVersionView view {};
    view.record.checkpoint =
        entity::CheckpointHandle { _w.items.history().lastWriteAt(found.key, checkpoint), 0 };
    view.record.kind = found.kind;
    view.record.parent = entity::INVALID_ITEM_HANDLE;
    view.record.id.assign(id);
    view.record.title = found.title;
    view.record.summary = found.summary;
    const auto parent_id = _w.items.history().idOf(found.parent);
    if (!parent_id) { return ViewResult::success(view); }
    view.parent_id = parent_id.value;
    const auto parent = _w.items.reader().findKey(found.parent);
    if (parent) { view.record.parent = parent.value; }
    return ViewResult::success(view);
}

Result<uint64_t, KiottySpecStatus> PastReader::tagAt(Span<const char> id, uint32_t tag,
                                                     uint32_t checkpoint)
{
    const auto version = versionAt(id, checkpoint);
    if (!version) { return Result<uint64_t, KiottySpecStatus>::failure(version.code); }
    return Result<uint64_t, KiottySpecStatus>::success(
        _w.items.history().tagAt(version.value->key, tag, checkpoint));
}

Result<size_t, KiottySpecStatus> PastReader::bodyAt(Span<const char> id, uint32_t checkpoint,
                                                    char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const auto version = versionAt(id, checkpoint);
    if (!version) { return SizeResult::failure(version.code); }
    const ItemVersion* current = _w.items.tables().versions.latest(version.value->key, UINT32_MAX);
    const bool same = current != nullptr && current->removed == 0 &&
                      current->body_hash == version.value->body_hash;
    if (!same) {
        return SizeResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_PAST_BODY, id));
    }
    return copyBody(_w.items.tables().bodies.find(version.value->key), buffer, capacity);
}

Result<size_t, KiottySpecStatus> PastReader::decisionAt(entity::ItemKind kind, uint32_t checkpoint,
                                                        char* buffer, size_t capacity)
{
    return _w.decisions.read(kind, checkpoint, buffer, capacity);
}

}
