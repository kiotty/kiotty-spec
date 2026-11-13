#pragma once

#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_differ.h"
#include "implementation/context/kiotty_finder.h"
#include "implementation/context/kiotty_opened_context.h"
#include "implementation/context/kiotty_rule_diff_store.h"
#include "implementation/context/kiotty_rule_differ.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/repository/checkpoint/kiotty_checkpoint_store.h"
#include "implementation/data/repository/decision/kiotty_decision_store.h"
#include "implementation/data/repository/item/kiotty_item_store.h"
#include "implementation/data/repository/meta/kiotty_meta_store.h"
#include "implementation/data/repository/relation/kiotty_relation_store.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/editor/kiotty_editor.h"
#include "implementation/domain/usecases/lifecycle/kiotty_lifecycle.h"
#include "implementation/data/repository/model/kiotty_model_catalog.h"
#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"
#include "implementation/data/repository/spec_file/kiotty_spec_file.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <optional>

namespace kiotty::spec::implementation {

constexpr size_t VERSION_FACTOR = 4;
constexpr size_t DEFAULT_MAX_ITEMS = 4096;
constexpr size_t DEFAULT_MAX_RELATIONS = 16384;
constexpr size_t DEFAULT_MAX_TAGS = 8192;
constexpr size_t DEFAULT_MAX_CHECKPOINTS = 4096;
constexpr size_t DEFAULT_MAX_DECISION_VERSIONS = 1024;
constexpr size_t DEFAULT_MAX_BODY_BYTES = 16 * 1024 * 1024;
constexpr size_t DEFAULT_MAX_CURSORS = 16;
constexpr size_t DEFAULT_MAX_CURSOR_ROWS = 4096;
constexpr size_t DEFAULT_WORK_BUFFER_BYTES = 256 * 1024;

struct Configuration {
    size_t max_items {DEFAULT_MAX_ITEMS};
    size_t max_relations {DEFAULT_MAX_RELATIONS};
    size_t max_tags {DEFAULT_MAX_TAGS};
    size_t max_checkpoints {DEFAULT_MAX_CHECKPOINTS};
    size_t max_decision_versions {DEFAULT_MAX_DECISION_VERSIONS};
    size_t max_body_bytes {DEFAULT_MAX_BODY_BYTES};
    size_t max_cursors {DEFAULT_MAX_CURSORS};
    size_t max_cursor_rows {DEFAULT_MAX_CURSOR_ROWS};
    size_t work_buffer_bytes {DEFAULT_WORK_BUFFER_BYTES};
    size_t max_item_versions {DEFAULT_MAX_ITEMS * VERSION_FACTOR};
    size_t max_tag_versions {DEFAULT_MAX_TAGS * VERSION_FACTOR};
    size_t max_relation_versions {DEFAULT_MAX_RELATIONS * VERSION_FACTOR};
    size_t cursor_pool_rows {DEFAULT_MAX_CURSORS * DEFAULT_MAX_CURSOR_ROWS};
};

static_assert(sizeof(Configuration) == 104, "unexpected padding");

class Context {
public:
    explicit Context(const Configuration& configuration);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    KiottySpecStatus open(Span<const char> database);
    OpenedContextResult opened();
    entity::Failure& failure() { return _failure; }

private:
    KiottySpecStatus checkDatabase(Span<const char> database);
    KiottySpecStatus loadSpecFile(Span<const char> absolute_path);
    KiottySpecStatus createSpecFile(Span<const char> directory);

    SystemPath _root {};
    SystemPath _database {};
    PathArea _paths {};
    PathResolver _path_resolver;
    FileSystem _file_system;
    entity::Failure _failure {};
    TextBuffer _work;
    ModelCatalog _catalog {};
    MetaStore _meta {};
    CheckpointStore _checkpoints;
    ItemStore _items;
    RelationStore _relations;
    DecisionStore _decisions;
    SpecFile _file;
    Workspace _workspace;
    Checker _checker;
    Lifecycle _lifecycle;
    Editor _editor;
    RelationEditor _relation_editor;
    CursorPool _cursors;
    TextDiffStore _text_diffs;
    Finder _finder;
    Differ _differ;
    RuleDiffStore _rule_diffs;
    RemoteState _remote;
    RuleAcceptor _rule_acceptor;
    RuleExporter _rule_exporter;
    RuleDiffer _rule_differ;
    std::optional<OpenedContext> _opened {};
};

}
