
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
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const RelationResult parsed = ValueParser(*spec.value).parseRelation(from_id, relation, to_id);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->writeSide().relationEditor().add(parsed.value));
}

KiottySpecStatusResult KiottySpec_removeRelation(KiottySpecContext* context, KiottySpecText from_id,
                                                 uint32_t relation, KiottySpecText to_id)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const RelationResult parsed = ValueParser(*spec.value).parseRelation(from_id, relation, to_id);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->writeSide().relationEditor().remove(parsed.value));
}

KiottySpecCursorResult KiottySpec_openRelationCursor(KiottySpecContext* context, KiottySpecText id,
                                                     KiottySpecDirection direction)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const DirectionResult parsed = QueryParser(*spec.value).parseDirection(direction);
    if (!parsed) { return cursorFailure(parsed.code); }
    const Boundary boundary(*spec.value);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    const impl::CursorIdResult opened =
        spec.value->readSide().finder().relations(handle.value, parsed.value);
    return boundary.wrap(opened);
}

KiottySpecCursorResult KiottySpec_openRelationCursorByModel(KiottySpecContext* context,
                                                            KiottySpecItemKind kind,
                                                            uint32_t relation)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const KindResult parsed_kind = ValueParser(*spec.value).parseKind(kind);
    if (!parsed_kind) { return cursorFailure(parsed_kind.code); }
    const FilterResult parsed_relation =
        QueryParser(*spec.value).parseRelationIndex(parsed_kind.value, relation);
    if (!parsed_relation) { return cursorFailure(parsed_relation.code); }
    const impl::CursorIdResult opened = spec.value->readSide().finder().relationsByModel(
        parsed_kind.value, parsed_relation.value);
    return Boundary(*spec.value).wrap(opened);
}

KiottySpecStatusResult KiottySpec_fetchRelationPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecRelationPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
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
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const QueryParser parser(*spec.value);
    const FilterResult parsed_kind = parser.parseKindFilter(kind);
    if (!parsed_kind) { return cursorFailure(parsed_kind.code); }
    const bool has_tag_queries = count == 0 || tag_queries != nullptr;
    const bool has_valid_count = count <= KIOTTY_SPEC_MAX_TAG_QUERIES && has_tag_queries;
    if (!has_valid_count) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::TagQuery queries[KIOTTY_SPEC_MAX_TAG_QUERIES] {};
    for (size_t index = 0; index < count; ++index) {
        const TagQueryResult query = parser.parseTagQuery(tag_queries[index]);
        if (!query) { return cursorFailure(query.code); }
        queries[index] = query.value;
    }
    const impl::CursorIdResult opened = spec.value->readSide().finder().items(
        parsed_kind.value, impl::Span<const impl::TagQuery>(queries, count));
    return Boundary(*spec.value).wrap(opened);
}

KiottySpecCursorResult KiottySpec_openChildCursor(KiottySpecContext* context, KiottySpecText id)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const Boundary boundary(*spec.value);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    return boundary.wrap(spec.value->readSide().finder().children(handle.value));
}

KiottySpecCursorResult KiottySpec_openImpactCursor(KiottySpecContext* context, KiottySpecText id,
                                                   uint32_t depth)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const Boundary boundary(*spec.value);
    const HandleResult handle = boundary.resolve(id);
    if (!handle) { return cursorFailure(handle.code); }
    return boundary.wrap(spec.value->readSide().finder().impact(handle.value, depth));
}

KiottySpecStatusResult KiottySpec_fetchItemPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                                KiottySpecItemPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    impl::ItemStore& items = spec.value->workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(cursor, impl::CursorKind::ITEM,
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
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    impl::ItemStore& items = spec.value->workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(cursor, impl::CursorKind::ID,
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
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    return status(spec.value->readSide().cursors().close(cursor));
}

}
