#include "kiotty_spec_adapter.h"

#include <cstring>

namespace kiotty::spec::adapter {

impl::Span<const char> spanOf(KiottySpecText text)
{
    if (text.data == nullptr) { return impl::Span<const char>(); }
    return impl::Span<const char>(text.data, text.length);
}

KiottySpecItemHandle toHandle(entity::ItemHandle handle)
{
    if (!handle.isValid()) { return KIOTTY_SPEC_INVALID_ITEM_HANDLE; }
    return impl::packHandle(handle);
}

entity::ItemHandle fromHandle(KiottySpecItemHandle handle) { return impl::unpackHandle(handle); }

KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle) { return handle.sequence; }

KiottySpecItem toItem(const entity::ItemRecord& record)
{
    KiottySpecItem item {};
    item.checkpoint = toCheckpoint(record.checkpoint);
    item.parent = toHandle(record.parent);
    item.kind = static_cast<KiottySpecItemKind>(record.kind);
    copyText(item.id, record.id);
    copyText(item.title, record.title);
    copyText(item.summary, record.summary);
    return item;
}

KiottySpecCheckpoint toCheckpointRecord(const entity::CheckpointRecord& record)
{
    KiottySpecCheckpoint checkpoint {};
    checkpoint.created_at = record.created_at;
    checkpoint.previous = toCheckpoint(record.previous);
    checkpoint.step = static_cast<KiottySpecStep>(record.step);
    checkpoint.state = static_cast<KiottySpecCheckpointState>(record.state);
    copyText(checkpoint.description, record.description);
    return checkpoint;
}

KiottySpecStatusResult status(KiottySpecStatus code) { return KiottySpecStatusResult{code}; }

KiottySpecCursorResult cursorResult(impl::Context& context, const impl::CursorIdResult& opened)
{
    if (!opened) { return KiottySpecCursorResult{opened.code, 0, 0}; }
    const auto total = context.cursors().total(opened.value);
    return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_SUCCESS, opened.value, total ? total.value : 0};
}

bool isKind(uint32_t kind) { return entity::isValidKind(kind); }

impl::Result<entity::ItemHandle, KiottySpecStatus> resolve(impl::Context& context, KiottySpecText id)
{
    const auto found = context.workspace().items.reader().find(spanOf(id));
    if (!found) {
        return impl::Result<entity::ItemHandle, KiottySpecStatus>::failure(
            context.failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {impl::literalText("없는 항목: "), spanOf(id)}));
    }
    return found;
}

KiottySpecSizeResult copyOut(impl::Span<const char> text, char* buffer, size_t capacity)
{
    if (buffer == nullptr || capacity <= text.length()) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, text.length() + 1}; }
    std::memcpy(buffer, text.data(), text.length());
    buffer[text.length()] = '\0';
    return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_SUCCESS, text.length()};
}

}
