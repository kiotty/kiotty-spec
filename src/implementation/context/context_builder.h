#pragma once

#include "implementation/data/repository/change_request_store.h"
#include "implementation/domain/usecases/checker.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/domain/usecases/milestone_service.h"
#include "implementation/data/repository/result_store.h"

#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

struct Stores {
    CheckpointStore &checkpoints;
    ItemStore &items;
    FileStore &files;
    LinkStore &links;
    MilestoneService &milestones;
    ChangeRequestStore &changes;
    DecisionStore &decisions;
    ResultStore &results;
    Checker &checker;
    const Failure &failure;
};

class ContextBuilder {
public:
    explicit ContextBuilder(Stores stores) : s_(stores) {}

    std::string forStep(int32_t step, size_t maxBytes);
    std::string forItem(std::string_view itemId, int32_t depth, size_t maxBytes);
    std::string forMilestone(std::string_view milestoneId, size_t maxBytes);
    std::string forChangeRequest(std::string_view crId, size_t maxBytes);
    std::string rules();
    std::string pending();
    std::string status();

    std::string renderItem(const ItemRecord &item, bool full);

private:
    Stores s_;
};

}
