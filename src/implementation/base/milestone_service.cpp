#include "implementation/base/milestone_service.h"

#include "implementation/core/error.h"

#include <set>

namespace kiotty::base {

using core::MilestoneEntryRecord;

void MilestoneService::setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplState state) {
    const auto milestone = items_.get(milestoneId);
    core::require(milestone.kind == KiottySpecItemKind_Milestone, KiottySpecStatus_InvalidArg, "마일스톤이 아님: " + milestone.id);
    core::require(links_.exists(milestoneId, itemId, KiottySpecLinkType_Covers), KiottySpecStatus_InvalidArg,
                  milestone.id + " 는 " + std::string(itemId) + " 를 포함하지 않음 (먼저 covers 링크 필요)");
    core::require(state == KiottySpecImplState_NotImplemented || state == KiottySpecImplState_NeedsChange ||
                      state == KiottySpecImplState_Implemented,
                  KiottySpecStatus_InvalidArg, "알 수 없는 구현 상태");
    const bool implemented = state == KiottySpecImplState_Implemented;
    const auto item = items_.get(itemId);
    db_.prepare("INSERT INTO milestone_state (milestone_id, item_id, cp_seq, state, implemented_wseq, implemented_cp_seq)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6)"
                " ON CONFLICT(milestone_id, item_id, cp_seq) DO UPDATE SET state = excluded.state,"
                " implemented_wseq = excluded.implemented_wseq, implemented_cp_seq = excluded.implemented_cp_seq")
        .bindAll(milestoneId, itemId, core::kPending, core::name(state),
                 implemented ? std::optional<int64_t>(item.wseq) : std::nullopt,
                 implemented ? std::optional<int64_t>(core::kPending) : std::nullopt)
        .run();
}

std::vector<MilestoneEntryRecord> MilestoneService::entries(std::string_view milestoneId, int64_t cut) {
    const auto milestone = items_.get(milestoneId, cut);
    core::require(milestone.kind == KiottySpecItemKind_Milestone, KiottySpecStatus_InvalidArg, "마일스톤이 아님: " + milestone.id);
    std::vector<MilestoneEntryRecord> result;
    for (const auto &itemId : links_.targets(milestoneId, KiottySpecLinkType_Covers, cut)) {
        const auto item = items_.find(itemId, cut);
        if (!item) {
            continue;
        }
        MilestoneEntryRecord entry;
        entry.milestoneId = milestone.id;
        entry.itemId = itemId;
        auto stmt = db_.prepare("SELECT state, implemented_wseq, implemented_cp_seq FROM milestone_state"
                                " WHERE milestone_id = ?1 AND item_id = ?2 AND cp_seq <= ?3 ORDER BY cp_seq DESC LIMIT 1");
        stmt.bindAll(milestoneId, itemId, cut);
        if (stmt.step()) {
            core::parse(stmt.text(0), entry.state);
            entry.implementedCpSeq = stmt.optInteger(2);
            const auto implementedWseq = stmt.optInteger(1);
            // 구현 완료 뒤 항목이 다시 쓰였으면 변경 필요.
            if (entry.state == KiottySpecImplState_Implemented && implementedWseq && item->wseq > *implementedWseq) {
                entry.state = KiottySpecImplState_NeedsChange;
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}

std::vector<core::ItemRecord> MilestoneService::uncovered(int64_t cut) {
    std::set<std::string> covered;
    for (const auto &milestone : items_.ofKind(KiottySpecItemKind_Milestone, cut)) {
        for (auto &id : links_.targets(milestone.id, KiottySpecLinkType_Covers, cut)) {
            covered.insert(std::move(id));
        }
    }
    std::vector<core::ItemRecord> result;
    for (const auto kind : {KiottySpecItemKind_Design, KiottySpecItemKind_UnitTest}) {
        for (auto &item : items_.ofKind(kind, cut)) {
            if (covered.count(item.id) == 0) {
                result.push_back(std::move(item));
            }
        }
    }
    return result;
}

std::optional<std::string> MilestoneService::next() {
    for (const auto &milestone : items_.ofKind(KiottySpecItemKind_Milestone)) {
        for (const auto &entry : entries(milestone.id)) {
            if (entry.state != KiottySpecImplState_Implemented) {
                return milestone.id;
            }
        }
    }
    return std::nullopt;
}

MilestoneProgress MilestoneService::progress() {
    MilestoneProgress progress;
    for (const auto &milestone : items_.ofKind(KiottySpecItemKind_Milestone)) {
        for (const auto &entry : entries(milestone.id)) {
            ++progress.total;
            if (entry.state == KiottySpecImplState_Implemented) ++progress.implemented;
            if (entry.state == KiottySpecImplState_NeedsChange) ++progress.needsChange;
        }
    }
    return progress;
}

} // namespace kiotty::base
