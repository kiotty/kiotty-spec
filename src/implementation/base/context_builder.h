#pragma once

#include "implementation/base/change_request_store.h"
#include "implementation/base/checker.h"
#include "implementation/base/checkpoint_store.h"
#include "implementation/base/decision_store.h"
#include "implementation/base/file_store.h"
#include "implementation/base/item_store.h"
#include "implementation/base/link_store.h"
#include "implementation/base/milestone_service.h"
#include "implementation/base/result_store.h"

#include <string>
#include <string_view>

namespace kiotty::base {

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
};

// AI 가 한 번의 질의로 받을 맥락을 토큰(바이트) 한도 안에서 Markdown 으로 묶는다.
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

    std::string renderItem(const core::ItemRecord &item, bool full);

private:
    Stores s_;
};

} // namespace kiotty::base
