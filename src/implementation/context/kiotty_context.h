#pragma once

#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_differ.h"
#include "implementation/context/kiotty_finder.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_editor.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"
#include "implementation/domain/usecases/kiotty_relation_editor.h"
#include "implementation/domain/usecases/kiotty_spec_file.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>

namespace kiotty::spec::implementation {

// 버전 표 용량은 현재 풀 용량의 VERSION_FACTOR 배다. 넘치면 정리(prune)로 줄인다.
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

// 라이브러리 인스턴스 하나가 쓰는 모든 것을 소유한다 (개발수칙 1.2, 설계서 v1 10.1).
// 풀 · 커서 · 버퍼는 생성자에서 확보하고, 소멸자가 전부 정리한다.
class Context {
public:
    explicit Context(const Configuration& configuration);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // .kisp 파일을 읽어 버전 표와 현재 뷰를 만든다. 없으면 디렉토리와 함께 빈 파일을 만든다.
    KiottySpecStatus open(Span<const char> database);
    KiottySpecStatus init(Span<const char> name);

    entity::Failure& failure() { return _failure; }
    Workspace& workspace() { return _workspace; }
    const ModelCatalog& catalog() const { return _catalog; }
    Editor& editor() { return _editor; }
    RelationEditor& relationEditor() { return _relation_editor; }
    Lifecycle& lifecycle() { return _lifecycle; }
    Finder& finder() { return _finder; }
    Differ& differ() { return _differ; }
    CursorPool& cursors() { return _cursors; }
    TextBuffer& work() { return _work; }

private:
    bool _opened {false};
    SystemPath _root;
    entity::Failure _failure;
    TextBuffer _work;
    ModelCatalog _catalog;
    MetaStore _meta;
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
    Finder _finder;
    Differ _differ;
};

}
