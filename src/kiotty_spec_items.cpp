// C 인터페이스: 항목 · 본문 · 태그. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

using RecordResult = impl::Result<const entity::ItemRecord*, KiottySpecStatus>;

RecordResult recordOf(impl::Context& context, KiottySpecItemHandle item)
{
    const RecordResult record = context.workspace().items.reader().get(fromHandle(item));
    if (!record) { return RecordResult::failure(context.failure().raise(record.code, KIOTTY_SPEC_SUBJECT_ITEM)); }
    return record;
}

}

extern "C" {

KiottySpecIdResult KiottySpec_createItem(KiottySpecContext* context, const KiottySpecItemDraft* draft)
{
    KiottySpecIdResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr || draft == nullptr) { return result; }
    const TextResult id = parseId(context->context, draft->id);
    if (!id) {
        result.code = id.code;
        return result;
    }
    const TextResult title = parseTitle(context->context, draft->title);
    if (!title) {
        result.code = title.code;
        return result;
    }
    const TextResult summary = parseSummary(context->context, draft->summary);
    if (!summary) {
        result.code = summary.code;
        return result;
    }
    const auto created = context->context.editor().createItem(static_cast<entity::ItemKind>(draft->kind), id.value, title.value, summary.value);
    result.code = created.code;
    if (created) { copyText(result.value, created.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_renameItem(KiottySpecContext* context, KiottySpecText id, KiottySpecText new_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult current = parseId(context->context, id);
    if (!current) { return status(current.code); }
    const TextResult next = parseId(context->context, new_id);
    if (!next) { return status(next.code); }
    return status(context->context.editor().renameItem(current.value, next.value));
}

KiottySpecItemResult KiottySpec_readItemByHandle(KiottySpecContext* context, KiottySpecItemHandle item)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const RecordResult record = recordOf(context->context, item);
    result.code = record.code;
    if (!record) { return result; }
    result.handle = item;
    result.value = toItem(*record.value);
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
    KiottySpecItemHandleResult result {};
    result.value = KIOTTY_SPEC_INVALID_ITEM_HANDLE;
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const auto handle = resolve(context->context, id);
    result.code = handle.code;
    if (handle) { result.value = toHandle(handle.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext* context, KiottySpecText id, KiottySpecText title, KiottySpecText summary)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed_id = parseId(context->context, id);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult parsed_title = parseTitle(context->context, title);
    if (!parsed_title) { return status(parsed_title.code); }
    const TextResult parsed_summary = parseSummary(context->context, summary);
    if (!parsed_summary) { return status(parsed_summary.code); }
    return status(context->context.editor().updateItem(parsed_id.value, parsed_title.value, parsed_summary.value));
}

KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext* context, KiottySpecText id, KiottySpecText parent_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult child = parseId(context->context, id);
    if (!child) { return status(child.code); }
    const TextResult parent = parseId(context->context, parent_id);
    if (!parent) { return status(parent.code); }
    return status(context->context.editor().setParent(child.value, parent.value));
}

KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed = parseId(context->context, id);
    if (!parsed) { return status(parsed.code); }
    return status(context->context.editor().removeItem(parsed.value));
}

KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext* context, KiottySpecText id, KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed_id = parseId(context->context, id);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult body = parseBody(context->context, text);
    if (!body) { return status(body.code); }
    return status(context->context.editor().writeBody(parsed_id.value, body.value));
}

KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext* context, KiottySpecText id, KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const auto handle = resolve(context->context, id);
    if (!handle) { return sizeResult(handle.code, 0); }
    const auto read = context->context.workspace().items.reader().readBody(handle.value, buffer.data, buffer.capacity);
    return sizeResult(read.code, read.value);
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
    const RecordResult record = recordOf(context->context, item);
    if (!record) {
        result.code = record.code;
        return result;
    }
    if (tag >= workspace.catalog.model(record.value->kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG, record.value->id.view());
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.value = toTag(item, static_cast<KiottySpecItemKind>(record.value->kind), tag, workspace.items.tags().value(fromHandle(item), tag));
    return result;
}

KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext* context, KiottySpecItemHandle item)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const RecordResult record = recordOf(context->context, item);
    if (!record) { return cursorFailure(record.code); }
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
        page->elements[index] = toTag(row.first, static_cast<KiottySpecItemKind>(row.second & LOW_MASK), static_cast<uint32_t>(row.second >> SHIFT), row.third);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}
