#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_differ.h"
#include "implementation/context/kiotty_finder.h"
#include "implementation/context/kiotty_rule_differ.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/editor/kiotty_editor.h"
#include "implementation/domain/usecases/lifecycle/kiotty_lifecycle.h"
#include "implementation/data/repository/model/kiotty_model_catalog.h"
#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"
#include "implementation/domain/usecases/remote/kiotty_remote_state.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_acceptor.h"
#include "implementation/context/kiotty_rule_exporter.h"
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

class RuleSyncSide
{
public:
    RuleSyncSide(RemoteState& remote, RuleAcceptor& acceptor, RuleExporter& exporter,
                 RuleDiffer& differ)
        : _remote(remote), _acceptor(acceptor), _exporter(exporter), _differ(differ)
    {}

    RemoteState& remote() const { return _remote; }
    RuleAcceptor& acceptor() const { return _acceptor; }
    RuleExporter& exporter() const { return _exporter; }
    RuleDiffer& differ() const { return _differ; }

private:
    RemoteState& _remote;
    RuleAcceptor& _acceptor;
    RuleExporter& _exporter;
    RuleDiffer& _differ;
};

struct ContextSides {
    ContextSides(const ContextWriteSide& write, const ContextReadSide& read,
                 const RuleSyncSide& sync)
        : write_side(write), read_side(read), rule_sync(sync)
    {}

    ContextWriteSide write_side;
    ContextReadSide read_side;
    RuleSyncSide rule_sync;
};

static_assert(sizeof(ContextSides) == 96, "unexpected padding");

class OpenedContext
{
public:
    OpenedContext(Workspace& workspace, const ContextSides& sides, const SystemPath& root)
        : _write_side(sides.write_side), _read_side(sides.read_side),
          _rule_sync(sides.rule_sync), _workspace(workspace), _root(root)
    {}

    OpenedContext(const OpenedContext&) = delete;
    OpenedContext& operator=(const OpenedContext&) = delete;

    KiottySpecStatus initialize(Span<const char> name);

    entity::Failure& failure() { return _workspace.failure; }
    Workspace& workspace() { return _workspace; }
    ContextWriteSide writeSide() const { return _write_side; }
    ContextReadSide readSide() const { return _read_side; }
    RuleSyncSide ruleSync() const { return _rule_sync; }

private:
    ContextWriteSide _write_side;
    ContextReadSide _read_side;
    RuleSyncSide _rule_sync;
    Workspace& _workspace;
    const SystemPath& _root;
};

using OpenedContextResult = Result<OpenedContext&, KiottySpecStatus>;

}
