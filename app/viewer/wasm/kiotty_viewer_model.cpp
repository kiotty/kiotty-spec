#include "kiotty_viewer_query.h"

#include "kiotty_viewer_names.h"

#include <memory>

namespace kiotty::viewer {

ModelDefinitions::ModelDefinitions(KiottySpecContext& context)
    : _context(context), _kinds(KIOTTY_SPEC_ITEM_KIND_COUNT + 1)
{
}

std::string ModelDefinitions::formatRelation(KiottySpecItemKind model, uint32_t relation)
{
    for (const KiottySpecRelationDefinition& definition : findKind(model).relations) {
        const bool is_match = definition.index == relation;
        if (is_match) { return definition.name; }
    }
    return std::to_string(relation);
}

const KindDefinitions& ModelDefinitions::findKind(KiottySpecItemKind kind)
{
    KindDefinitions& entry = _kinds.at(static_cast<size_t>(kind));
    if (entry.is_loaded) { return entry; }
    entry.is_loaded = true;
    entry.tags = readTags(kind);
    entry.relations = readRelations(kind);
    return entry;
}

std::vector<KiottySpecTagDefinition> ModelDefinitions::readTags(KiottySpecItemKind kind)
{
    std::vector<KiottySpecTagDefinition> tags {};
    const KiottySpecCursorResult cursor = KiottySpec_openTagDefinitionCursor(&_context, kind);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) { return tags; }
    const std::unique_ptr<KiottySpecTagDefinitionPage> page_storage =
        std::make_unique<KiottySpecTagDefinitionPage>();
    KiottySpecTagDefinitionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchTagDefinitionPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            tags.push_back(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    return tags;
}

std::vector<KiottySpecRelationDefinition> ModelDefinitions::readRelations(KiottySpecItemKind kind)
{
    std::vector<KiottySpecRelationDefinition> relations {};
    const KiottySpecCursorResult cursor = KiottySpec_openRelationDefinitionCursor(&_context, kind);
    const bool is_open = isSuccess(cursor.code);
    if (!is_open) { return relations; }
    const std::unique_ptr<KiottySpecRelationDefinitionPage> page_storage =
        std::make_unique<KiottySpecRelationDefinitionPage>();
    KiottySpecRelationDefinitionPage& page = *page_storage;
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchRelationDefinitionPage(&_context, cursor.value, &page);
        const bool has_rows = isSuccess(fetched.code) && page.length > 0;
        if (!has_rows) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            relations.push_back(page.elements[index]);
        }
    }
    KiottySpec_closeCursor(&_context, cursor.value);
    return relations;
}

}
