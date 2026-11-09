#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/repository/change_request_store.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_graph.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/result_store.h"
#include "implementation/data/repository/version_history.h"
#include "implementation/domain/usecases/checker.h"
#include "implementation/domain/usecases/milestone_service.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct Stores {
    CheckpointStore &checkpoints;
    ItemStore &items;
    ItemFinder &finder;
    FileStore &files;
    LinkStore &links;
    LinkGraph &graph;
    VersionHistory &history;
    MilestoneService &milestones;
    ChangeRequestStore &changes;
    DecisionStore &decisions;
    ResultStore &results;
    Checker &checker;
    Failure &failure;
};

TextResult renderItem(LinkStore &links, const ItemRecord &item, bool full);

class Budget;

class ContextBuilder {
public:
    explicit ContextBuilder(Stores stores) : _s(stores) {}

    TextResult forStep(int32_t step, size_t maxBytes);
    TextResult forItem(std::string_view itemId, int32_t depth, size_t maxBytes);
    TextResult forMilestone(std::string_view milestoneId, size_t maxBytes);
    TextResult forChangeRequest(std::string_view crId, size_t maxBytes);
    TextResult rules();

private:
    KiottySpecStatus stepChanges(Budget &budget);
    KiottySpecStatus stepSections(Budget &budget, int32_t step, size_t maxBytes);
    KiottySpecStatus section(Budget &budget, std::string_view title, const ItemListResult &items);
    KiottySpecStatus kindSection(Budget &budget, std::string_view title, KiottySpecItemKind kind);
    KiottySpecStatus decision(Budget &budget, KiottySpecDecisionKind kind);
    KiottySpecStatus fileSection(Budget &budget, std::string_view title, std::initializer_list<KiottySpecFileRole> roles);
    KiottySpecStatus related(Budget &budget, const ItemRecord &item, int32_t depth);

    Stores _s;
};

class StatusBoard {
public:
    explicit StatusBoard(Stores stores) : _s(stores) {}

    TextResult pending();
    TextResult status();

private:
    KiottySpecStatus pendingChanges(std::string &out);
    KiottySpecStatus pendingMilestones(std::string &out);
    KiottySpecStatus testTable(std::string &out);

    Stores _s;
};

}
