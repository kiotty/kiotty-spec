#pragma once

#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/records.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct MilestoneProgress {
    size_t total = 0;
    size_t implemented = 0;
    size_t needsChange = 0;
};

class MilestoneService {
public:
    MilestoneService(Database &db, Meta &meta, ItemStore &items, LinkStore &links, const Failure &failure)
        : db_(db), meta_(meta), items_(items), links_(links), failure_(failure) {}

    void setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state);
    std::vector<MilestoneEntryRecord> entries(std::string_view milestoneId, int64_t cut = kCurrent);
    std::vector<ItemRecord> uncovered(int64_t cut = kCurrent);
    std::optional<std::string> next();
    MilestoneProgress progress();

private:
    Database &db_;
    Meta &meta_;
    ItemStore &items_;
    LinkStore &links_;
    const Failure &failure_;
};

}
