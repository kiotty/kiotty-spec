#include "kiotty_spec_adapter.h"
#include "implementation/context/kiotty_model_definition_finder.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

using TagDefinitions = impl::Span<KiottySpecTagDefinition>;
using RelationDefinitions = impl::Span<KiottySpecRelationDefinition>;

KiottySpecStatus writeTagDefinition(const impl::ModelFinder& finder, const impl::CursorRow& row,
                                    TagDefinitions elements, size_t position);
KiottySpecStatus writeRelationDefinition(const impl::ModelFinder& finder,
                                         const impl::CursorRow& row,
                                         RelationDefinitions elements, size_t position);

}

extern "C" {

KiottySpecModelResult KiottySpec_readModel(KiottySpecContext* context, KiottySpecItemKind kind)
{
    KiottySpecModelResult result {};
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const KindResult parsed = ValueParser(*spec.value).parseKind(kind);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    const entity::Model& model = spec.value->readSide().catalog().model(parsed.value);
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, model.name);
    copyText(result.value.id_prefix, model.id_prefix);
    result.value.kind = kind;
    result.value.created_in = static_cast<KiottySpecStep>(model.created_in);
    result.value.tag_count = static_cast<uint32_t>(model.tags.length);
    result.value.relation_count = static_cast<uint32_t>(model.relations.length);
    return result;
}

KiottySpecCursorResult KiottySpec_openTagDefinitionCursor(KiottySpecContext* context,
                                                          KiottySpecItemKind kind)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const KindResult parsed = ValueParser(*spec.value).parseKind(kind);
    if (!parsed) { return cursorFailure(parsed.code); }
    impl::ModelDefinitionFinder finder(spec.value->workspace(), spec.value->readSide().cursors());
    const impl::CursorIdResult opened =
        finder.openTagDefinitions(parsed.value);
    return Boundary(*spec.value).wrap(opened);
}

KiottySpecStatusResult KiottySpec_fetchTagDefinitionPage(KiottySpecContext* context,
                                                         KiottySpecCursor cursor,
                                                         KiottySpecTagDefinitionPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
        cursor, impl::CursorKind::TAG_DEFINITION, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::ModelFinder finder = spec.value->readSide().catalog().finder();
    const TagDefinitions elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const KiottySpecStatus written = writeTagDefinition(finder, row, elements, page->length);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return status(written); }
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openRelationDefinitionCursor(KiottySpecContext* context,
                                                               KiottySpecItemKind kind)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const KindResult parsed = ValueParser(*spec.value).parseKind(kind);
    if (!parsed) { return cursorFailure(parsed.code); }
    impl::ModelDefinitionFinder finder(spec.value->workspace(), spec.value->readSide().cursors());
    const impl::CursorIdResult opened =
        finder.openRelationDefinitions(parsed.value);
    return Boundary(*spec.value).wrap(opened);
}

KiottySpecStatusResult
KiottySpec_fetchRelationDefinitionPage(KiottySpecContext* context, KiottySpecCursor cursor,
                                       KiottySpecRelationDefinitionPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
        cursor, impl::CursorKind::RELATION_DEFINITION, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::ModelFinder finder = spec.value->readSide().catalog().finder();
    const RelationDefinitions elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const KiottySpecStatus written =
            writeRelationDefinition(finder, row, elements, page->length);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return status(written); }
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}

namespace {

KiottySpecStatus writeTagDefinition(const impl::ModelFinder& finder, const impl::CursorRow& row,
                                    TagDefinitions elements, size_t position)
{
    const auto element = elements.at(position);
    if (!element) { return element.code; }
    const entity::ItemKind kind = static_cast<entity::ItemKind>(row.first);
    const uint32_t index = static_cast<uint32_t>(row.number);
    const auto source = finder.tagsOf(kind).at(index);
    if (!source) { return source.code; }
    KiottySpecTagDefinition& definition = *element.value;
    copyText(definition.name, source.value->name);
    const impl::Span<const entity::TagName> bit_names = finder.bitNamesOf(kind, index);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        copyText(definition.bit_names[bit], bit_names[bit]);
    }
    definition.cardinality = static_cast<KiottySpecCardinality>(source.value->cardinality);
    definition.index = index;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus writeRelationDefinition(const impl::ModelFinder& finder,
                                         const impl::CursorRow& row,
                                         RelationDefinitions elements, size_t position)
{
    const auto element = elements.at(position);
    if (!element) { return element.code; }
    const entity::ItemKind kind = static_cast<entity::ItemKind>(row.first);
    const uint32_t index = static_cast<uint32_t>(row.number);
    const auto source = finder.relationsOf(kind).at(index);
    if (!source) { return source.code; }
    KiottySpecRelationDefinition& definition = *element.value;
    definition = KiottySpecRelationDefinition{};
    copyText(definition.name, source.value->name);
    definition.target_kind = static_cast<KiottySpecItemKind>(source.value->target_kind);
    definition.cardinality = static_cast<KiottySpecCardinality>(source.value->cardinality);
    definition.index = index;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
