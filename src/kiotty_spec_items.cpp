
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

KiottySpecIdResult makeIdFailure(KiottySpecStatus code);

}

extern "C" {

KiottySpecIdResult KiottySpec_createItem(KiottySpecContext* context,
                                         const KiottySpecItemDraft* draft)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return makeIdFailure(spec.code); }
    if (draft == nullptr) { return makeIdFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(*spec.value);
    const TextResult id = boundary.parse(draft->id, Field::ID);
    if (!id) { return makeIdFailure(id.code); }
    const TextResult title = boundary.parse(draft->title, Field::TITLE);
    if (!title) { return makeIdFailure(title.code); }
    const TextResult summary = boundary.parse(draft->summary, Field::SUMMARY);
    if (!summary) { return makeIdFailure(summary.code); }
    const KindResult kind = ValueParser(*spec.value).parseKind(draft->kind);
    if (!kind) { return makeIdFailure(kind.code); }
    KiottySpecIdResult result {};
    const auto created = spec.value->writeSide().editor().structure().createItem(
        kind.value, id.value, title.value, summary.value);
    result.code = created.code;
    if (created) { copyText(result.value, created.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_renameItem(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecText new_id)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const Boundary boundary(*spec.value);
    const TextResult current = boundary.parse(id, Field::ID);
    if (!current) { return status(current.code); }
    const TextResult next = boundary.parseNewId(new_id);
    if (!next) { return status(next.code); }
    return status(
        spec.value->writeSide().editor().structure().renameItem(current.value, next.value));
}

KiottySpecItemResult KiottySpec_readItem(KiottySpecContext* context, KiottySpecText id)
{
    KiottySpecItemResult result {};
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const HandleResult handle = Boundary(*spec.value).resolve(id);
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
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const RecordResult record = Boundary(*spec.value).read(item);
    result.code = record.code;
    if (!record) { return result; }
    const LabelResult label = spec.value->workspace().items.reader().label(fromHandle(item));
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
    const OpenedResult spec = findOpenedContext(context);
    result.code = spec ? KIOTTY_SPEC_STATUS_INVALID_ARGUMENT : spec.code;
    if (!spec) { return result; }
    const HandleResult handle = Boundary(*spec.value).resolve(id);
    result.code = handle.code;
    if (handle) { result.value = toHandle(handle.value); }
    return result;
}

KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecText title, KiottySpecText summary)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const Boundary boundary(*spec.value);
    const TextResult parsed_id = boundary.parse(id, Field::ID);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult parsed_title = boundary.parse(title, Field::TITLE);
    if (!parsed_title) { return status(parsed_title.code); }
    const TextResult parsed_summary = boundary.parse(summary, Field::SUMMARY);
    if (!parsed_summary) { return status(parsed_summary.code); }
    return status(spec.value->writeSide().editor().content().updateItem(
        parsed_id.value, toOptionalText(parsed_title.value), toOptionalText(parsed_summary.value)));
}

KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext* context, KiottySpecText id,
                                                KiottySpecText parent_id)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const Boundary boundary(*spec.value);
    const TextResult child = boundary.parse(id, Field::ID);
    if (!child) { return status(child.code); }
    const TextResult parent = boundary.parse(parent_id, Field::ID);
    if (!parent) { return status(parent.code); }
    return status(
        spec.value->writeSide().editor().structure().setParent(child.value, parent.value));
}

KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext* context, KiottySpecText id)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const TextResult parsed = Boundary(*spec.value).parse(id, Field::ID);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->writeSide().editor().structure().removeItem(parsed.value));
}

KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext* context, KiottySpecText id,
                                                KiottySpecText text)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const Boundary boundary(*spec.value);
    const TextResult parsed_id = boundary.parse(id, Field::ID);
    if (!parsed_id) { return status(parsed_id.code); }
    const TextResult body = boundary.parse(text, Field::BODY);
    if (!body) { return status(body.code); }
    return status(
        spec.value->writeSide().editor().content().writeBody(parsed_id.value, body.value));
}

KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext* context, KiottySpecText id,
                                             KiottySpecBuffer buffer)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return sizeResult(spec.code, 0); }
    const HandleResult handle = Boundary(*spec.value).resolve(id);
    if (!handle) { return sizeResult(handle.code, 0); }
    const impl::Span<char> target = toSpan(buffer);
    const auto read = spec.value->workspace().items.reader().readBody(handle.value, target);
    return sizeResult(read.code, read.value);
}

KiottySpecStatusResult KiottySpec_setItemTag(KiottySpecContext* context, KiottySpecItemHandle item,
                                             uint32_t tag, uint64_t value)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const TagResult parsed = ValueParser(*spec.value).parseTag(item, tag, value);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->writeSide().editor().content().setTag(parsed.value));
}

KiottySpecTagResult KiottySpec_readItemTag(KiottySpecContext* context, KiottySpecItemHandle item,
                                           uint32_t tag)
{
    KiottySpecTagResult result {};
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const TagResult parsed = ValueParser(*spec.value).parseTag(item, tag, 0);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    const entity::TagRecord& found = parsed.value;
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.value = toTag(item, static_cast<KiottySpecItemKind>(found.model), found.tag,
                         spec.value->workspace().items.tags().value(found.item, found.tag));
    return result;
}

KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext* context,
                                                KiottySpecItemHandle item)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const Boundary boundary(*spec.value);
    const RecordResult record = boundary.read(item);
    if (!record) { return cursorFailure(record.code); }
    return boundary.wrap(spec.value->readSide().finder().tags(fromHandle(item)));
}

KiottySpecStatusResult KiottySpec_fetchTagPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                               KiottySpecTagPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(cursor, impl::CursorKind::TAG,
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

namespace {

KiottySpecIdResult makeIdFailure(KiottySpecStatus code)
{
    KiottySpecIdResult result {};
    result.code = code;
    return result;
}

}
