#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_differ.h"
#include "implementation/context/kiotty_finder.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/editor/kiotty_editor.h"
#include "implementation/domain/usecases/lifecycle/kiotty_lifecycle.h"
#include "implementation/data/repository/model/kiotty_model_catalog.h"
#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

namespace kiotty::spec::implementation {

class ContextWriteSide
{
public:
    ContextWriteSide(Editor& editor, RelationEditor& relation_editor, Lifecycle& lifecycle,
                     Checker& checker)
        : _editor(editor), _relation_editor(relation_editor), _lifecycle(lifecycle),
          _checker(checker)
    {}

    Editor& editor() const { return _editor; }
    RelationEditor& relationEditor() const { return _relation_editor; }
    Lifecycle& lifecycle() const { return _lifecycle; }
    KiottySpecStatus completeCheckpoint(entity::Actor actor) const;

private:
    Editor& _editor;
    RelationEditor& _relation_editor;
    Lifecycle& _lifecycle;
    Checker& _checker;
};

class ContextReadSide
{
public:
    ContextReadSide(const ModelCatalog& catalog, Finder& finder, Differ& differ,
                    CursorPool& cursors)
        : _catalog(catalog), _finder(finder), _differ(differ), _cursors(cursors)
    {}

    const ModelCatalog& catalog() const { return _catalog; }
    Finder& finder() const { return _finder; }
    Differ& differ() const { return _differ; }
    CursorPool& cursors() const { return _cursors; }

private:
    const ModelCatalog& _catalog;
    Finder& _finder;
    Differ& _differ;
    CursorPool& _cursors;
};

class OpenedContext
{
public:
    OpenedContext(Workspace& workspace, const ContextWriteSide& write_side,
                  const ContextReadSide& read_side, const SystemPath& root)
        : _write_side(write_side), _read_side(read_side), _workspace(workspace), _root(root)
    {}

    OpenedContext(const OpenedContext&) = delete;
    OpenedContext& operator=(const OpenedContext&) = delete;

    KiottySpecStatus initialize(Span<const char> name);

    entity::Failure& failure() { return _workspace.failure; }
    Workspace& workspace() { return _workspace; }
    ContextWriteSide writeSide() const { return _write_side; }
    ContextReadSide readSide() const { return _read_side; }

private:
    ContextWriteSide _write_side;
    ContextReadSide _read_side;
    Workspace& _workspace;
    const SystemPath& _root;
};

using OpenedContextResult = Result<OpenedContext&, KiottySpecStatus>;

}
