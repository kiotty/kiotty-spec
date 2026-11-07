#pragma once

#include "implementation/data/repository/change_request_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/domain/usecases/milestone_service.h"
#include "implementation/data/repository/result_store.h"

#include <string>
#include <vector>

namespace kiotty::spec::implementation {

struct OneToOne {
    std::vector<ItemRecord> missing;
    std::vector<ItemRecord> duplicated;
};

struct Verdict {
    bool ok = true;
    std::string report;
};

class Checker {
public:
    Checker(ItemStore &items, LinkStore &links, MilestoneService &milestones, ResultStore &results,
            DecisionStore &decisions, ChangeRequestStore &changes, const Failure &failure)
        : items_(items), links_(links), milestones_(milestones), results_(results), decisions_(decisions),
          changes_(changes), failure_(failure) {}

    std::vector<ItemRecord> requirementsWithoutCriteria(int64_t cut = kCurrent);
    OneToOne oneToOne(KiottySpecItemKind testKind, int64_t cut = kCurrent);
    std::vector<ItemRecord> unlinked(KiottySpecItemKind kind, int64_t cut = kCurrent);
    std::vector<ItemRecord> orphanTests(int64_t cut = kCurrent);

    Verdict confirmCheck(int32_t step, int64_t cut = kCurrent);
    Verdict releaseCheck();
    std::string all();

    static bool hasAcceptanceCriteria(const std::string &body);

private:
    ItemStore &items_;
    LinkStore &links_;
    MilestoneService &milestones_;
    ResultStore &results_;
    DecisionStore &decisions_;
    ChangeRequestStore &changes_;
    const Failure &failure_;
};

std::string bulletList(const std::vector<ItemRecord> &items);

}
