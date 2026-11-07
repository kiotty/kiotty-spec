#pragma once

#include "implementation/data/repository/change_request_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/result_store.h"
#include "implementation/domain/usecases/milestone_service.h"

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

using OneToOneResult = Result<OneToOne, KiottySpecStatus>;
using VerdictResult = Result<Verdict, KiottySpecStatus>;

struct CheckSources {
    ItemStore &items;
    ItemFinder &finder;
    LinkStore &links;
    MilestoneService &milestones;
    ResultStore &results;
    DecisionStore &decisions;
    ChangeRequestStore &changes;
};

class Checker {
public:
    explicit Checker(CheckSources sources) : _s(sources) {}

    ItemListResult requirementsWithoutCriteria(int64_t cut = kCurrent);
    OneToOneResult oneToOne(KiottySpecItemKind testKind, int64_t cut = kCurrent);
    ItemListResult unlinked(KiottySpecItemKind kind, int64_t cut = kCurrent);
    ItemListResult orphanTests(int64_t cut = kCurrent);
    VerdictResult confirmCheck(int32_t step, int64_t cut = kCurrent);
    VerdictResult releaseCheck();
    TextResult all();

private:
    CountResult verifierCount(const ItemRecord &target, KiottySpecItemKind testKind, int64_t cut);
    FlagResult hasTarget(const ItemRecord &test, int64_t cut);
    ItemListResult notPassed(KiottySpecItemKind kind);
    TextResult decisionProblems();

    CheckSources _s;
};

bool hasAcceptanceCriteria(const std::string &body);
std::string bulletList(const std::vector<ItemRecord> &items);

}
