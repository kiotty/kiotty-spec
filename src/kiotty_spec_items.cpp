
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecIdResult KiottySpec_createItem(KiottySpecContext* context,
                                         const KiottySpecItemDraft* draft)
{
    KiottySpecIdResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr || draft == nullptr) { return result; }
    const Boundary boundary(context->context);
    const TextResult id = boundary.parse(draft->id, Field::ID);
    if (!id) {
        result.code = id.code;
        return result;
    }
    const TextResult title = boundary.parse(draft->title, Field::TITLE);
    if (!title) {
        result.code = title.code;
        return result;
    }
    const TextResult summary = boundary.parse(draft->summary, Field::SUMMARY);
    if (!summary) {
        result.code = summary.code;
        return result;
    }
    const auto created = context->context.writeSide().editor().structure().createItem(
        static_cast<entity::ItemKind>(draft->kind), id.value, title.value, summary.value);
    result.code = created.code;
    if (created) { copyText(result.value, created.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_renameItem(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecText new_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult current = boundary.parse(id, Field::ID);
    if (!current) { return status(current.code); }
    const TextResult next = boundary.parse(new_id, Field::ID);
    if (!next) { return status(next.code); }
    return status(
        context->context.writeSide().editor().structure().renameItem(current.value, next.value));
}

KiottySpecItemResult KiottySpec_readItem(KiottySpecContext* context, KiottySpecText id)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const HandleResult handle = Boundary(context->context).resolve(id);
    if (!handle) {
        result.code = handle.code;
        return result;
    }
    return KiottySpec_readItemByHandle(context, toHandle(handle.value));
}

KiottySpecItemResult KiottySpec_readItemByHandle(KiottySpecContext* context,
                                                 KiottySpecItemHandle item)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const RecordResult record = Boundary(context->context).read(item);
    result.code = record.code;
    if (!record) { return result; }
    const LabelResult label = context->context.workspace().items.reader().label(fromHandle(item));
    result.code = label.code;
    if (!label) { return result; }
    result.handle = item;
    result.value = toItem(*record.value, *label.value);
    return result;
}

KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext* context, KiottySpecText id)
{
    KiottySpecItemHandleResult result {};
    result.value = KIOTTY_SPEC_INVALID_ITEM_HANDLE;
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const HandleResult handle = Boundary(context->context).resolve(id);
    result.code = handle.code;
    if (handle) { result.value = toHandle(handle.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecText title, KiottySpecText summary)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult parsed_id = boundary.parse(id, Field::ID);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult parsed_title = boundary.parse(title, Field::TITLE);
    if (!parsed_title) { return status(parsed_title.code); }
    const TextResult parsed_summary = boundary.parse(summary, Field::SUMMARY);
    if (!parsed_summary) { return status(parsed_summary.code); }
    return status(context->context.writeSide().editor().content().updateItem(
        parsed_id.value, toOptionalText(parsed_title.value), toOptionalText(parsed_summary.value)));
}

KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext* context, KiottySpecText id,
                                                KiottySpecText parent_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult child = boundary.parse(id, Field::ID);
    if (!child) { return status(child.code); }
    const TextResult parent = boundary.parse(parent_id, Field::ID);
    if (!parent) { return status(parent.code); }
    return status(
        context->context.writeSide().editor().structure().setParent(child.value, parent.value));
}

KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed = Boundary(context->context).parse(id, Field::ID);
    if (!parsed) { return status(parsed.code); }
    return status(context->context.writeSide().editor().structure().removeItem(parsed.value));
}

KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext* context, KiottySpecText id,
                                                KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult parsed_id = boundary.parse(id, Field::ID);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult body = boundary.parse(text, Field::BODY);
    if (!body) { return status(body.code); }
    return status(
        context->context.writeSide().editor().content().writeBody(parsed_id.value, body.value));
}

KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const HandleResult handle = Boundary(context->context).resolve(id);
    if (!handle) { return sizeResult(handle.code, 0); }
    const impl::Span<char> target = toSpan(buffer);
    const auto read = context->context.workspace().items.reader().readBody(handle.value, target);
    return sizeResult(read.code, read.value);
}

KiottySpecStatusResult KiottySpec_setItemTag(KiottySpecContext* context, KiottySpecItemHandle item,
                                             uint32_t tag, uint64_t value)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(
        context->context.writeSide().editor().content().setTag(fromHandle(item), tag, value));
}

KiottySpecTagResult KiottySpec_readItemTag(KiottySpecContext* context, KiottySpecItemHandle item,
                                           uint32_t tag)
{
    KiottySpecTagResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Workspace& workspace = context->context.workspace();
    const RecordResult record = Boundary(context->context).read(item);
    if (!record) {
        result.code = record.code;
        return result;
    }
    const size_t tag_count = workspace.catalog.model(record.value->kind).tags.length;
    if (tag >= tag_count) {
        result.code = context->context.failure().raise(
            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG,
            workspace.items.reader().label(fromHandle(item)).value->id.view());
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.value = toTag(item, static_cast<KiottySpecItemKind>(record.value->kind), tag,
                         workspace.items.tags().value(fromHandle(item), tag));
    return result;
}

KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext* context,
                                                KiottySpecItemHandle item)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const RecordResult record = boundary.read(item);
    if (!record) { return cursorFailure(record.code); }
    return boundary.wrap(context->context.readSide().finder().tags(fromHandle(item)));
}

KiottySpecStatusResult KiottySpec_fetchTagPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                               KiottySpecTagPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.readSide().cursors().fetch(cursor, impl::CursorKind::TAG,
                                                                  KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecTag> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const auto element = elements.at(page->length);
        if (!element) { return status(element.code); }
        *element.value = toTag(row.first, static_cast<KiottySpecItemKind>(row.second & LOW_MASK),
                               static_cast<uint32_t>(row.second >> SHIFT), row.third);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}
