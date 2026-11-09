#pragma once

#include "implementation/context/context_builder.h"
#include "implementation/core/failure.h"
#include "implementation/data/datasource/connection.h"
#include "implementation/data/datasource/file_system.h"
#include "implementation/data/datasource/project_files.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/change_request_store.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_graph.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/milestone_store.h"
#include "implementation/data/repository/result_store.h"
#include "implementation/data/repository/version_history.h"
#include "implementation/domain/usecases/checker.h"
#include "implementation/domain/usecases/exchange.h"
#include "implementation/domain/usecases/journey.h"
#include "implementation/domain/usecases/lifecycle.h"
#include "implementation/domain/usecases/milestone_service.h"

#include <string_view>

namespace kiotty::spec::implementation {

enum class TransactionStep { BEGIN, COMMIT, ROLLBACK };

// Project 가 소유한 store 와 usecase 를 빌려 쓰는 참조 묶음.
struct Services {
    Storage &storage;
    Meta &meta;
    CheckpointStore &checkpoints;
    ItemStore &items;
    ItemFinder &finder;
    FileStore &files;
    LinkStore &links;
    LinkGraph &graph;
    MilestoneService &milestones;
    ChangeRequestStore &changes;
    DecisionStore &decisions;
    ResultStore &results;
    Checker &checker;
    ContextBuilder &context;
    StatusBoard &board;
    Exchange &exchange;
    Journey &journey;
    Lifecycle &lifecycle;
    const Failure &failure;
};

class Project {
public:
    Project();

    Project(const Project &) = delete;
    Project &operator=(const Project &) = delete;

    KiottySpecStatus open(std::string_view root);
    KiottySpecStatus init(std::string_view projectName, bool installGitHooks);
    KiottySpecStatus transaction(TransactionStep step);
    SystemPathResult jsonlDir();
    Span<const char> root() const { return _root.view(); }
    Services &services() { return _services; }
    const Failure &failure() const { return _failure; }

private:
    KiottySpecStatus prepareRoot(std::string_view root);
    KiottySpecStatus openDatabase();
    KiottySpecStatus updateGitignore();
    KiottySpecStatus installHooks();
    SystemPathResult under(Span<const char> base, Span<const char> part);
    KiottySpecStatus writeText(Span<const char> path, Span<const char> content);

    SystemPath _root;
    bool _opened {false};
    Connection _connection;
    Failure _failure;
    Storage _storage;
    ProjectFiles _projectFiles;
    Meta _meta;
    CheckpointStore _checkpoints;
    VersionHistory _history;
    ItemStore _items;
    ItemFinder _finder;
    FileStore _files;
    LinkStore _links;
    LinkGraph _graph;
    MilestoneStore _milestoneStore;
    MilestoneService _milestones;
    ChangeRequestStore _changes;
    DecisionStore _decisions;
    ResultStore _results;
    Checker _checker;
    ContextBuilder _context;
    StatusBoard _board;
    Exchange _exchange;
    Journey _journey;
    Lifecycle _lifecycle;
    Services _services;
};

}
