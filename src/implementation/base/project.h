#pragma once

#include "implementation/base/change_request_store.h"
#include "implementation/base/checker.h"
#include "implementation/base/checkpoint_store.h"
#include "implementation/base/context_builder.h"
#include "implementation/base/decision_store.h"
#include "implementation/base/exchange.h"
#include "implementation/base/file_store.h"
#include "implementation/base/item_store.h"
#include "implementation/base/journey.h"
#include "implementation/base/link_store.h"
#include "implementation/base/meta.h"
#include "implementation/base/milestone_service.h"
#include "implementation/base/result_store.h"
#include "implementation/core/database.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace kiotty::base {

// 열린 프로젝트 하나. DB 와 모든 모듈을 소유하고, 여러 모듈에 걸친 규칙(R-2, R-10 등)을 맡는다.
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
    core::Database db_;

public:
    // 선언 순서가 곧 초기화 순서다. 앞 모듈을 뒤 모듈이 참조한다.
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

    core::Database &database() { return db_; }
};

} // namespace kiotty::base
