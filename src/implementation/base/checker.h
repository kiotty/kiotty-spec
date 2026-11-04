#pragma once

#include "implementation/base/change_request_store.h"
#include "implementation/base/decision_store.h"
#include "implementation/base/item_store.h"
#include "implementation/base/link_store.h"
#include "implementation/base/milestone_service.h"
#include "implementation/base/result_store.h"

#include <string>
#include <vector>

namespace kiotty::base {

struct OneToOne {
    std::vector<core::ItemRecord> missing;     // 테스트가 없는 대상
    std::vector<core::ItemRecord> duplicated;  // 테스트가 둘 이상인 대상
};

struct Verdict {
    bool ok = true;
    std::string report;  // Markdown
};

// 불일치 검출과 단계별 확정 조건.
class Checker {
public:
    Checker(ItemStore &items, LinkStore &links, MilestoneService &milestones, ResultStore &results,
            DecisionStore &decisions, ChangeRequestStore &changes)
        : items_(items), links_(links), milestones_(milestones), results_(results), decisions_(decisions),
          changes_(changes) {}

    std::vector<core::ItemRecord> requirementsWithoutCriteria(int64_t cut = core::kCurrent);
    OneToOne oneToOne(KiottySpecItemKind testKind, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> unlinked(KiottySpecItemKind kind, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> orphanTests(int64_t cut = core::kCurrent);

    Verdict confirmCheck(int32_t step, int64_t cut = core::kCurrent);
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
};

// 항목 목록을 "- ID 제목" 줄들로.
std::string bulletList(const std::vector<core::ItemRecord> &items);

} // namespace kiotty::base
