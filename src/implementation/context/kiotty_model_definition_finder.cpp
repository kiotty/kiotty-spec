#include "implementation/context/kiotty_model_definition_finder.h"

namespace kiotty::spec::implementation {

CursorIdResult ModelDefinitionFinder::openTagDefinitions(entity::ItemKind kind)
{
    const size_t tag_count = _workspace.catalog.model(kind).tags.length;
    return openIndexCursor(CursorKind::TAG_DEFINITION, kind, tag_count);
}

CursorIdResult ModelDefinitionFinder::openRelationDefinitions(entity::ItemKind kind)
{
    const size_t relation_count = _workspace.catalog.model(kind).relations.length;
    return openIndexCursor(CursorKind::RELATION_DEFINITION, kind, relation_count);
}

CursorIdResult ModelDefinitionFinder::openIndexCursor(CursorKind cursor_kind, entity::ItemKind kind,
                                                      size_t count)
{
    const auto cursor = _cursors.open(cursor_kind);
    CursorWriter writer(_cursors, _workspace.failure);
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    for (size_t index = 0; index < count; ++index) {
        CursorRow row {};
        row.first = static_cast<uint64_t>(kind);
        row.number = static_cast<int64_t>(index);
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

}
