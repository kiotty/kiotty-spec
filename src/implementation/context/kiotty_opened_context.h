#pragma once

#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
#include "implementation/domain/usecases/diff/kiotty_differ.h"
#include "implementation/domain/usecases/query/kiotty_finder.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_differ.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/infra/platform/kiotty_system_path.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/editor/kiotty_editor.h"
#include "implementation/domain/usecases/lifecycle/kiotty_lifecycle.h"
#include "implementation/data/repository/model/kiotty_model_catalog.h"
#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"
#include "implementation/domain/usecases/remote/kiotty_remote_state.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_acceptor.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_exporter.h"
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

struct ReadParts {
    Finder& finder;
    Differ& differ;
    CursorPool& cursors;
};

static_assert(sizeof(ReadParts) == 24, "unexpected padding");

class ContextReadSide
{
public:
    ContextReadSide(const ModelCatalog& catalog, const ReadParts& parts, Checker& checker)
        : _catalog(catalog), _parts(parts), _checker(checker)
    {}

    const ModelCatalog& catalog() const { return _catalog; }
    Finder& finder() const { return _parts.finder; }
    Differ& differ() const { return _parts.differ; }
    CursorPool& cursors() const { return _parts.cursors; }
    CursorIdResult unmetConditions() const;
    Result<entity::Position, KiottySpecStatus> position() const;

private:
    const ModelCatalog& _catalog;
    ReadParts _parts;
    Checker& _checker;
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

static_assert(sizeof(ContextSides) == 104, "unexpected padding");

struct ContextPaths {
    const SystemPath& root;
    const PathResolver& resolver;
    const FileSystem& files;
};

static_assert(sizeof(ContextPaths) == 24, "unexpected padding");

class OpenedContext
{
public:
    OpenedContext(Workspace& workspace, const ContextSides& sides, const ContextPaths& paths)
        : _write_side(sides.write_side), _read_side(sides.read_side),
          _rule_sync(sides.rule_sync), _workspace(workspace), _paths(paths)
    {}

    OpenedContext(const OpenedContext&) = delete;
    OpenedContext& operator=(const OpenedContext&) = delete;

    KiottySpecStatus initialize(Span<const char> name);

    entity::Failure& failure() { return _workspace.failure; }
    Workspace& workspace() { return _workspace; }
    ContextWriteSide writeSide() const { return _write_side; }
    ContextReadSide readSide() const { return _read_side; }
    RuleSyncSide ruleSync() const { return _rule_sync; }
    KiottySpecStatus exportRules(Span<const char> path);

private:
    ContextWriteSide _write_side;
    ContextReadSide _read_side;
    RuleSyncSide _rule_sync;
    Workspace& _workspace;
    ContextPaths _paths;
};

using OpenedContextResult = Result<OpenedContext&, KiottySpecStatus>;

}
