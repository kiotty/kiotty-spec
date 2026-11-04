#pragma once

#include "implementation/base/item_store.h"
#include "implementation/base/link_store.h"
#include "implementation/base/meta.h"
#include "implementation/core/database.h"
#include "implementation/core/records.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::base {

struct MilestoneProgress {
    size_t total = 0;
    size_t implemented = 0;
    size_t needsChange = 0;
};

// 마일스톤 포함 항목의 구현 상태. 저장된 상태와 설계 항목의 쓰기 순번을 비교해 계산한다 (R-6).
class MilestoneService {
public:
    MilestoneService(core::Database &db, Meta &meta, ItemStore &items, LinkStore &links)
        : db_(db), meta_(meta), items_(items), links_(links) {}

    void setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplState state);
    std::vector<core::MilestoneEntryRecord> entries(std::string_view milestoneId, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> uncovered(int64_t cut = core::kCurrent);
    std::optional<std::string> next();
    MilestoneProgress progress();

private:
    core::Database &db_;
    Meta &meta_;
    ItemStore &items_;
    LinkStore &links_;
};

} // namespace kiotty::base
