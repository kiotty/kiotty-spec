#pragma once

#include "implementation/data/repository/change_request_store.h"
#include "implementation/domain/usecases/checker.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/context/context_builder.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/domain/usecases/exchange.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/domain/usecases/journey.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/meta.h"
#include "implementation/domain/usecases/milestone_service.h"
#include "implementation/data/repository/result_store.h"
#include "implementation/data/datasource/database.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class Project {
public:
    explicit Project(const std::filesystem::path &root);

    const std::filesystem::path &root() const { return root_; }
    std::filesystem::path specDir() const { return root_ / ".kiotty"; }
    std::filesystem::path jsonlDir() const { return specDir() / "spec"; }

    void init(std::string_view projectName, bool installGitHooks);

    int64_t confirm(KiottySpecActor actor, std::string_view cpId);
    int64_t release(KiottySpecActor actor, std::string_view note);
    size_t prune(KiottySpecActor actor, std::string_view baseCpId);
    void removeItem(std::string_view itemId);

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

private:
    static std::filesystem::path prepareRoot(const std::filesystem::path &root);
    void installHooks();

    std::filesystem::path root_;
    Database db_;

public:
    Meta meta;
    CheckpointStore checkpoints;
    ItemStore items;
    FileStore files;
    LinkStore links;
    MilestoneService milestones;
    ChangeRequestStore changes;
    DecisionStore decisions;
    ResultStore results;
    Checker checker;
    ContextBuilder context;
    Exchange exchange;
    Journey journey;

    Database &database() { return db_; }
};

}
