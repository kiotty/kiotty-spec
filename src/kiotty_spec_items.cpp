// C 인터페이스: 항목 · 본문 · 태그. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecIdResult KiottySpec_createItem(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText title, KiottySpecText summary)
{
    KiottySpecIdResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto created = context->context.editor().createItem(static_cast<entity::ItemKind>(kind), spanOf(title), spanOf(summary));
    result.code = created.code;
    if (created) { copyText(result.value, created.value); }
    return result;
}

KiottySpecItemResult KiottySpec_readItemByHandle(KiottySpecContext* context, KiottySpecItemHandle item)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto record = context->context.workspace().items.reader().get(fromHandle(item));
    result.code = record.code;
    if (record) {
        result.handle = item;
        result.value = toItem(*record.value);
    }
    return result;
}

KiottySpecItemResult KiottySpec_readItem(KiottySpecContext* context, KiottySpecText id)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto handle = resolve(context->context, id);
    if (!handle) {
        result.code = handle.code;
        return result;
    }
    return KiottySpec_readItemByHandle(context, toHandle(handle.value));
}

KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return KiottySpecItemHandleResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_INVALID_ITEM_HANDLE}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecItemHandleResult{handle.code, KIOTTY_SPEC_INVALID_ITEM_HANDLE}; }
    return KiottySpecItemHandleResult{KIOTTY_SPEC_STATUS_SUCCESS, toHandle(handle.value)};
}

KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext* context, KiottySpecText id, KiottySpecText title, KiottySpecText summary)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().updateItem(spanOf(id), spanOf(title), spanOf(summary)));
}

KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext* context, KiottySpecText id, KiottySpecText parent_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().setParent(spanOf(id), spanOf(parent_id)));
}

KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().removeItem(spanOf(id)));
}

KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext* context, KiottySpecText id, KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().writeBody(spanOf(id), spanOf(text)));
}

KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext* context, KiottySpecText id, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto handle = resolve(context->context, id);
    if (!handle) { return KiottySpecSizeResult{handle.code, 0}; }
    const auto read = context->context.workspace().items.reader().readBody(handle.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecStatusResult KiottySpec_setItemTag(KiottySpecContext* context, KiottySpecItemHandle item, uint32_t tag, uint64_t value)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().setTag(fromHandle(item), tag, value));
}

KiottySpecTagResult KiottySpec_readItemTag(KiottySpecContext* context, KiottySpecItemHandle item, uint32_t tag)
{
    KiottySpecTagResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Workspace& workspace = context->context.workspace();
    const auto record = workspace.items.reader().get(fromHandle(item));
    if (!record) {
        result.code = record.code;
        return result;
    }
    if (tag >= workspace.catalog.model(record.value->kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("모델에 없는 태그 index")});
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.value = KiottySpecTag{item, static_cast<KiottySpecItemKind>(record.value->kind), tag, workspace.items.tags().value(fromHandle(item), tag)};
    return result;
}

KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext* context, KiottySpecItemHandle item)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    if (!context->context.workspace().items.reader().get(fromHandle(item))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_HANDLE, 0, 0}; }
    return cursorResult(context->context, context->context.finder().tags(fromHandle(item)));
}

KiottySpecStatusResult KiottySpec_fetchTagPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecTagPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::TAG, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        page->elements[index] = KiottySpecTag{row.first, static_cast<KiottySpecItemKind>(row.second & 0xFFFFFFFFu),
                                              static_cast<uint32_t>(row.second >> SHIFT), row.third};
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}
