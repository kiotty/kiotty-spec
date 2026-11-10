#pragma once

#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_differ.h"
#include "implementation/context/kiotty_finder.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_db_connection.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_editor.h"
#include "implementation/domain/usecases/kiotty_exchange.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_relation_editor.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>

namespace kiotty::spec::implementation {

struct Configuration {
    size_t maxItems {4096};
    size_t maxRelations {16384};
    size_t maxTags {8192};
    size_t maxCheckpoints {4096};
    size_t maxCursors {16};
    size_t maxCursorRows {4096};
    size_t workBufferBytes {256 * 1024};
};

// 라이브러리 인스턴스 하나가 쓰는 모든 것을 소유한다 (개발수칙 1.2, 설계서 v1 10.1).
// 풀 · 커서 · 버퍼는 생성자에서 확보하고, 소멸자가 전부 정리한다.
class Context {
public:
    explicit Context(const Configuration& configuration);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // .kisp 파일을 열고 (없으면 디렉토리와 함께 만든다) DB 의 현재 버전을 풀에 적재한다.
    // 스키마 1 DB 는 <파일>.v1 로 옮기고 빈 DB 를 만든다. 스키마 2 는 그 자리에서 3 으로 바꾼다.
    // 프로젝트 루트는 파일이 든 디렉토리의 상위로 본다 (<루트>/.kiotty/spec.kisp). init 과 git 훅이 쓴다.
    KiottySpecStatus open(Span<const char> database);
    KiottySpecStatus init(Span<const char> name, bool install_hooks);
    // DB 파일 옆에 확장자를 뺀 이름의 디렉토리 (.kiotty/spec.kisp → .kiotty/spec)
    SystemPathResult jsonlDirectory();
    bool movedLegacyDatabase() const { return _moved_legacy; }

    Failure& failure() { return _failure; }
    Workspace& workspace() { return _workspace; }
    const ModelCatalog& catalog() const { return _catalog; }
    Editor& editor() { return _editor; }
    RelationEditor& relationEditor() { return _relation_editor; }
    Lifecycle& lifecycle() { return _lifecycle; }
    Exchange& exchange() { return _exchange; }
    Finder& finder() { return _finder; }
    Differ& differ() { return _differ; }
    CursorPool& cursors() { return _cursors; }
    TextBuffer& work() { return _work; }

private:
    KiottySpecStatus openDatabase();
    KiottySpecStatus moveLegacyDatabase(Span<const char> database);
    KiottySpecStatus updateGitignore();
    KiottySpecStatus installHooks();
    SystemPathResult under(Span<const char> base, Span<const char> part);

    SystemPath _database;
    SystemPath _root;
    bool _opened {false};
    bool _moved_legacy {false};
    Failure _failure;
    DBConnection _connection;
    Storage _storage;
    ModelCatalog _catalog;
    MetaStore _meta;
    CheckpointStore _checkpoints;
    ItemStore _items;
    RelationStore _relations;
    DecisionStore _decisions;
    Workspace _workspace;
    Checker _checker;
    Lifecycle _lifecycle;
    Editor _editor;
    RelationEditor _relation_editor;
    Exchange _exchange;
    CursorPool _cursors;
    Finder _finder;
    TextBuffer _work;
    Differ _differ;
};

}
