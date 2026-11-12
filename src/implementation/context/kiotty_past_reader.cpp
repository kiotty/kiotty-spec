#include "implementation/context/kiotty_past_reader.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemVersion;
using VersionResult = Result<const ItemVersion&, KiottySpecStatus>;

}

Result<ItemVersionView, KiottySpecStatus> PastReader::itemAt(Span<const char> id,
                                                             uint32_t checkpoint)
{
    using ViewResult = Result<ItemVersionView, KiottySpecStatus>;
    const auto version = versionAt(id, checkpoint);
    if (!version) { return ViewResult::failure(version.code); }
    const ItemVersion& found = *version.value;
    ItemVersionView view {};
    const uint32_t written = _workspace.items.history().lastWriteAt(found.key, checkpoint);
    view.record.checkpoint = entity::CheckpointHandle { written, 0 };
    view.record.kind = found.kind;
    view.record.parent = entity::INVALID_ITEM_HANDLE;
    view.record.id.assign(id);
    view.record.title = found.title;
    view.record.summary = found.summary;
    const auto parent_id = _workspace.items.history().idOf(found.parent);
    if (!parent_id) { return ViewResult::success(view); }
    view.parent_id = parent_id.value;
    const auto parent = _workspace.items.reader().findKey(found.parent);
    if (parent) { view.record.parent = parent.value; }
    return ViewResult::success(view);
}

Result<uint64_t, KiottySpecStatus> PastReader::tagAt(Span<const char> id, uint32_t tag,
                                                     uint32_t checkpoint)
{
    const auto version = versionAt(id, checkpoint);
    if (!version) { return Result<uint64_t, KiottySpecStatus>::failure(version.code); }
    const uint32_t key = (*version.value).key;
    return Result<uint64_t, KiottySpecStatus>::success(
        _workspace.items.history().tagAt(key, tag, checkpoint));
}

Result<size_t, KiottySpecStatus> PastReader::readBodyAt(Span<const char> id, uint32_t checkpoint,
                                                        Span<char> buffer)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const auto version = versionAt(id, checkpoint);
    if (!version) { return SizeResult::failure(version.code); }
    const ItemVersion& past = *version.value;
    const VersionResult current = _workspace.items.tables().versions.latest(past.key, UINT32_MAX);
    const bool is_same_body = current && (*current.value).removed == 0 &&
                              (*current.value).body_hash == past.body_hash;
    if (!is_same_body) {
        return SizeResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_PAST_BODY, id));
    }
    return copyBody(_workspace.items.tables().bodies.find(past.key), buffer);
}

Result<size_t, KiottySpecStatus> PastReader::readDecisionAt(entity::ItemKind kind,
                                                            uint32_t checkpoint, Span<char> buffer)
{
    return _workspace.decisions.read(kind, checkpoint, buffer);
}

Result<const ItemVersion&, KiottySpecStatus> PastReader::versionAt(Span<const char> id,
                                                                   uint32_t checkpoint)
{
    const auto key = _workspace.items.history().keyOf(id);
    const ItemTables& tables = _workspace.items.tables();
    const VersionResult version = key ? tables.versions.latest(key.value, checkpoint)
                                      : VersionResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    const bool is_live = version && (*version.value).removed == 0;
    if (!is_live) {
        return VersionResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id));
    }
    return version;
}

}
