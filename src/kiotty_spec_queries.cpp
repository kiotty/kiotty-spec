
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

using IdElement = char[KIOTTY_SPEC_MAX_ID_LENGTH + 1];

}

extern "C" {

KiottySpecStatusResult KiottySpec_addRelation(KiottySpecContext* context, KiottySpecText from_id,
                                              uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult from = boundary.parse(from_id, Field::ID);
    if (!from) { return status(from.code); }
    const TextResult to = boundary.parse(to_id, Field::ID);
    if (!to) { return status(to.code); }
    return status(
        context->context.writeSide().relationEditor().add(from.value, relation, to.value));
}

KiottySpecStatusResult KiottySpec_removeRelation(KiottySpecContext* context, KiottySpecText from_id,
                                                 uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const TextResult from = boundary.parse(from_id, Field::ID);
    if (!from) { return status(from.code); }
    const TextResult to = boundary.parse(to_id, Field::ID);
    if (!to) { return status(to.code); }
    return status(
        context->context.writeSide().relationEditor().remove(from.value, relation, to.value));
}

KiottySpecCursorResult KiottySpec_openRelationCursor(KiottySpecContext* context, KiottySpecText id,
                                                     KiottySpecDirection direction)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (direction < KIOTTY_SPEC_DIRECTION_UPSTREAM || direction > KIOTTY_SPEC_DIRECTION_BOTH) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const Boundary boundary(context->context);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    const impl::CursorIdResult opened =
        context->context.readSide().finder().relations(handle.value, direction);
    return boundary.wrap(opened);
}

KiottySpecCursorResult KiottySpec_openRelationCursorByModel(KiottySpecContext* context,
                                                            KiottySpecItemKind kind,
                                                            uint32_t relation)
{
    const bool is_valid_kind = isKind(kind);
    const bool has_valid_arguments = context != nullptr && is_valid_kind;
    if (!has_valid_arguments) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const size_t relation_count = context->context.readSide()
                                      .catalog()
                                      .model(static_cast<entity::ItemKind>(kind))
                                      .relations.length;
    if (relation >= relation_count) {
        return cursorFailure(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                              KIOTTY_SPEC_SUBJECT_RELATION));
    }
    const impl::CursorIdResult opened = context->context.readSide().finder().relationsByModel(
        static_cast<entity::ItemKind>(kind), relation);
    return Boundary(context->context).wrap(opened);
}

KiottySpecStatusResult KiottySpec_fetchRelationPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecRelationPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.readSide().cursors().fetch(
        cursor, impl::CursorKind::RELATION, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecRelation> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const auto slot = elements.at(page->length);
        if (!slot) { return status(slot.code); }
        KiottySpecRelation& element = *slot.value;
        element = KiottySpecRelation{};
        element.from = row.first;
        element.to = row.second;
        element.model = static_cast<KiottySpecItemKind>(row.third & LOW_MASK);
        element.relation = static_cast<uint32_t>(row.third >> SHIFT);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openItemCursor(KiottySpecContext* context,
                                                 KiottySpecItemKind kind,
                                                 const KiottySpecTagQuery* tag_queries,
                                                 size_t count)
{
    const bool is_kind = isKind(kind);
    const bool is_accepted_kind = kind == 0 || is_kind;
    const bool has_tag_queries = count == 0 || tag_queries != nullptr;
    const bool has_valid_arguments = context != nullptr && is_accepted_kind &&
                                     count <= KIOTTY_SPEC_MAX_TAG_QUERIES && has_tag_queries;
    if (!has_valid_arguments) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    impl::TagQuery queries[KIOTTY_SPEC_MAX_TAG_QUERIES] {};
    for (size_t index = 0; index < count; ++index) {
        const bool is_query_kind = isKind(tag_queries[index].kind);
        if (!is_query_kind) {
            return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
        }
        queries[index] = impl::TagQuery { tag_queries[index].mask,
                                          static_cast<entity::ItemKind>(tag_queries[index].kind),
                                          tag_queries[index].tag_index };
    }
    const impl::CursorIdResult opened = context->context.readSide().finder().items(
        kind, impl::Span<const impl::TagQuery>(queries, count));
    return Boundary(context->context).wrap(opened);
}

KiottySpecCursorResult KiottySpec_openChildCursor(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    return boundary.wrap(context->context.readSide().finder().children(handle.value));
}

KiottySpecCursorResult KiottySpec_openImpactCursor(KiottySpecContext* context, KiottySpecText id,
                                                   uint32_t depth)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const Boundary boundary(context->context);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    return boundary.wrap(context->context.readSide().finder().impact(handle.value, depth));
}

KiottySpecStatusResult KiottySpec_fetchItemPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                                KiottySpecItemPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.readSide().cursors().fetch(cursor, impl::CursorKind::ITEM,
                                                                  KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecItem> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const RecordResult record = items.reader().get(fromHandle(row.first));
        const LabelResult label = items.reader().label(fromHandle(row.first));
        const bool is_live = record && label;
        if (!is_live) { continue; }
        const auto element = elements.at(page->length);
        if (!element) { return status(element.code); }
        *element.value = toItem(*record.value, *label.value);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecStatusResult KiottySpec_fetchIdPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                              KiottySpecIdPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.readSide().cursors().fetch(cursor, impl::CursorKind::ID,
                                                                  KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<IdElement> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const LabelResult label = items.reader().label(fromHandle(row.first));
        if (!label) { continue; }
        const auto element = elements.at(page->length);
        if (!element) { return status(element.code); }
        copyText(*element.value, label.value->id);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecStatusResult KiottySpec_closeCursor(KiottySpecContext* context, KiottySpecCursor cursor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.readSide().cursors().close(cursor));
}

}
