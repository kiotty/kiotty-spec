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
#include "implementation/domain/usecases/milestone_service.h"

#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class Project {
public:
    explicit Project(std::string_view root);

    Span<const char> root() const { return root_.view(); }
    SystemPath jsonlDir() const;

    void init(std::string_view projectName, bool installGitHooks);

    int64_t confirm(KiottySpecActor actor, std::string_view cpId);
    int64_t release(KiottySpecActor actor, std::string_view note);
    size_t prune(KiottySpecActor actor, std::string_view baseCpId);
    void removeItem(std::string_view itemId);

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

private:
    static SystemPath prepareRoot(std::string_view root);
    void open();
    void installHooks();

    SystemPath root_;
    Connection connection_;
    Failure failure_;
    Storage storage_;
    ProjectFiles projectFiles_;

public:
    Meta meta;
    CheckpointStore checkpoints;
    VersionHistory history;
    ItemStore items;
    ItemFinder finder;
    FileStore files;
    LinkStore links;
    LinkGraph graph;
    MilestoneStore milestoneStore;
    MilestoneService milestones;
    ChangeRequestStore changes;
    DecisionStore decisions;
    ResultStore results;
    Checker checker;
    ContextBuilder context;
    Exchange exchange;
    Journey journey;

    Storage &storage() { return storage_; }
    const Failure &failure() const { return failure_; }
};

}
