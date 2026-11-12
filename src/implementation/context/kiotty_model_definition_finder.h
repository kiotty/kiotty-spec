#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>

namespace kiotty::spec::implementation {

class ModelDefinitionFinder
{
public:
    ModelDefinitionFinder(Workspace& workspace, CursorPool& cursors)
        : _workspace(workspace), _cursors(cursors)
    {}

    CursorIdResult openTagDefinitions(entity::ItemKind kind);
    CursorIdResult openRelationDefinitions(entity::ItemKind kind);

private:
    CursorIdResult openIndexCursor(CursorKind cursor_kind, entity::ItemKind kind, size_t count);

    Workspace& _workspace;
    CursorPool& _cursors;
};

}
