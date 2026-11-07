#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/milestone_store.h"
#include "implementation/domain/entity/records.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct MilestoneProgress {
    size_t total = 0;
    size_t implemented = 0;
    size_t needsChange = 0;
};

using MilestoneEntryListResult = Result<std::vector<MilestoneEntryRecord>, KiottySpecStatus>;
using OptionalMilestoneEntryResult = Result<std::optional<MilestoneEntryRecord>, KiottySpecStatus>;
using MilestoneProgressResult = Result<MilestoneProgress, KiottySpecStatus>;

class MilestoneService {
public:
    MilestoneService(MilestoneStore &store, ItemStore &items, ItemFinder &finder, LinkStore &links, Failure &failure)
        : _store(store), _items(items), _finder(finder), _links(links), _failure(failure) {}

    KiottySpecStatus setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state);
    MilestoneEntryListResult entries(std::string_view milestoneId, int64_t cut = kCurrent);
    ItemListResult uncovered(int64_t cut = kCurrent);
    OptionalTextResult next();
    MilestoneProgressResult progress();

private:
    ItemResult milestone(std::string_view milestoneId, int64_t cut);
    OptionalMilestoneEntryResult entryOf(const ItemRecord &milestone, std::string_view itemId, int64_t cut);

    MilestoneStore &_store;
    ItemStore &_items;
    ItemFinder &_finder;
    LinkStore &_links;
    Failure &_failure;
};

}
