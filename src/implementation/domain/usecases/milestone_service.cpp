#include "implementation/domain/usecases/milestone_service.h"

#include "implementation/core/error.h"

#include <set>

namespace kiotty::spec::implementation {

void MilestoneService::setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state) {
    const auto milestone = expect(items_.get(milestoneId), failure_);
    require(milestone.kind == KIOTTY_SPEC_ITEM_KIND_MILESTONE, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "마일스톤이 아님: " + milestone.id);
    require(expect(links_.exists(milestoneId, itemId, KIOTTY_SPEC_LINK_TYPE_COVERS), failure_), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                  milestone.id + " 는 " + std::string(itemId) + " 를 포함하지 않음 (먼저 covers 링크 필요)");
    require(state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED || state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE ||
                      state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED,
                  KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 구현 상태");
    const bool implemented = state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED;
    const auto item = expect(items_.get(itemId), failure_);
    db_.prepare("INSERT INTO milestone_state (milestone_id, item_id, cp_seq, state, implemented_wseq, implemented_cp_seq)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6)"
                " ON CONFLICT(milestone_id, item_id, cp_seq) DO UPDATE SET state = excluded.state,"
                " implemented_wseq = excluded.implemented_wseq, implemented_cp_seq = excluded.implemented_cp_seq")
        .bindAll(milestoneId, itemId, kPending, name(state),
                 implemented ? std::optional<int64_t>(item.wseq) : std::nullopt,
                 implemented ? std::optional<int64_t>(kPending) : std::nullopt)
        .run();
}

std::vector<MilestoneEntryRecord> MilestoneService::entries(std::string_view milestoneId, int64_t cut) {
    const auto milestone = expect(items_.get(milestoneId, cut), failure_);
    require(milestone.kind == KIOTTY_SPEC_ITEM_KIND_MILESTONE, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "마일스톤이 아님: " + milestone.id);
    std::vector<MilestoneEntryRecord> result;
    for (const auto &itemId : expect(links_.targets(milestoneId, KIOTTY_SPEC_LINK_TYPE_COVERS, cut), failure_)) {
        const auto item = expect(items_.find(itemId, cut), failure_);
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
            parse(stmt.text(0), entry.state);
            entry.implementedCpSeq = stmt.optInteger(2);
            const auto implementedWseq = stmt.optInteger(1);
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED && implementedWseq && item->wseq > *implementedWseq) {
                entry.state = KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE;
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}

std::vector<ItemRecord> MilestoneService::uncovered(int64_t cut) {
    std::set<std::string> covered;
    for (const auto &milestone : expect(items_.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE, cut), failure_)) {
        for (auto &id : expect(links_.targets(milestone.id, KIOTTY_SPEC_LINK_TYPE_COVERS, cut), failure_)) {
            covered.insert(std::move(id));
        }
    }
    std::vector<ItemRecord> result;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST}) {
        for (auto &item : expect(items_.ofKind(kind, cut), failure_)) {
            if (covered.count(item.id) == 0) {
                result.push_back(std::move(item));
            }
        }
    }
    return result;
}

std::optional<std::string> MilestoneService::next() {
    for (const auto &milestone : expect(items_.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE), failure_)) {
        for (const auto &entry : entries(milestone.id)) {
            if (entry.state != KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) {
                return milestone.id;
            }
        }
    }
    return std::nullopt;
}

MilestoneProgress MilestoneService::progress() {
    MilestoneProgress progress;
    for (const auto &milestone : expect(items_.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE), failure_)) {
        for (const auto &entry : entries(milestone.id)) {
            ++progress.total;
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) ++progress.implemented;
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE) ++progress.needsChange;
        }
    }
    return progress;
}

}
