#include "implementation/domain/usecases/milestone_service.h"

#include "implementation/core/text_view.h"

#include <set>
#include <string>

namespace kiotty::spec::implementation {

namespace {

bool isKnownState(KiottySpecImplementationState state)
{
    return state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED || state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE ||
           state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED;
}

KiottySpecImplementationState effectiveState(const MilestoneState &stored, const ItemRecord &item)
{
    const bool isStale = stored.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED && stored.implementedWseq &&
                         item.wseq > *stored.implementedWseq;
    if (isStale) { return KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE; }
    return stored.state;
}

}

KiottySpecStatus MilestoneService::setEntryState(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state)
{
    const auto found = milestone(milestoneId, kCurrent);
    if (!found) { return found.code; }
    const auto covered = _links.exists(milestoneId, itemId, KIOTTY_SPEC_LINK_TYPE_COVERS);
    if (!covered) { return covered.code; }
    if (!covered.value) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {found.value.id.view(), literalText(" 는 "), spanOf(itemId), literalText(" 를 포함하지 않음 (먼저 covers 링크 필요)")}); }
    if (!isKnownState(state)) { return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 구현 상태")}); }
    const auto item = _items.get(itemId);
    if (!item) { return item.code; }
    const bool implemented = state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED;
    return _store.write(milestoneId, itemId, state, implemented ? std::optional<int64_t>(item.value.wseq) : std::nullopt);
}

MilestoneEntryListResult MilestoneService::entries(std::string_view milestoneId, int64_t cut)
{
    const auto found = milestone(milestoneId, cut);
    if (!found) { return MilestoneEntryListResult::failure(found.code); }
    const auto itemIds = _links.targets(milestoneId, KIOTTY_SPEC_LINK_TYPE_COVERS, cut);
    if (!itemIds) { return MilestoneEntryListResult::failure(itemIds.code); }
    std::vector<MilestoneEntryRecord> result;
    for (const std::string &itemId : itemIds.value) {
        const auto entry = entryOf(found.value, itemId, cut);
        if (!entry) { return MilestoneEntryListResult::failure(entry.code); }
        if (entry.value) { result.push_back(*entry.value); }
    }
    return MilestoneEntryListResult::success(std::move(result));
}

ItemListResult MilestoneService::uncovered(int64_t cut)
{
    const auto milestones = _finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE, cut);
    if (!milestones) { return milestones; }
    std::set<std::string> covered;
    for (const ItemRecord &milestone : milestones.value) {
        auto ids = _links.targets(milestone.id, KIOTTY_SPEC_LINK_TYPE_COVERS, cut);
        if (!ids) { return ItemListResult::failure(ids.code); }
        for (std::string &id : ids.value) { covered.insert(std::move(id)); }
    }
    std::vector<ItemRecord> result;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST}) {
        auto items = _finder.ofKind(kind, cut);
        if (!items) { return items; }
        for (ItemRecord &item : items.value) {
            if (covered.count(std::string(item.id)) == 0) { result.push_back(std::move(item)); }
        }
    }
    return ItemListResult::success(std::move(result));
}

OptionalTextResult MilestoneService::next()
{
    const auto milestones = _finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE);
    if (!milestones) { return OptionalTextResult::failure(milestones.code); }
    for (const ItemRecord &milestone : milestones.value) {
        const auto list = entries(milestone.id);
        if (!list) { return OptionalTextResult::failure(list.code); }
        for (const MilestoneEntryRecord &entry : list.value) {
            if (entry.state != KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) { return OptionalTextResult::success(std::string(milestone.id)); }
        }
    }
    return OptionalTextResult::success(std::nullopt);
}

MilestoneProgressResult MilestoneService::progress()
{
    const auto milestones = _finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE);
    if (!milestones) { return MilestoneProgressResult::failure(milestones.code); }
    MilestoneProgress progress;
    for (const ItemRecord &milestone : milestones.value) {
        const auto list = entries(milestone.id);
        if (!list) { return MilestoneProgressResult::failure(list.code); }
        for (const MilestoneEntryRecord &entry : list.value) {
            ++progress.total;
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) { ++progress.implemented; }
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE) { ++progress.needsChange; }
        }
    }
    return MilestoneProgressResult::success(progress);
}

ItemResult MilestoneService::milestone(std::string_view milestoneId, int64_t cut)
{
    auto found = _items.get(milestoneId, cut);
    if (!found) { return found; }
    if (found.value.kind == KIOTTY_SPEC_ITEM_KIND_MILESTONE) { return found; }
    return ItemResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("마일스톤이 아님: "), found.value.id.view()}));
}

OptionalMilestoneEntryResult MilestoneService::entryOf(const ItemRecord &milestone, std::string_view itemId, int64_t cut)
{
    const auto item = _items.find(itemId, cut);
    if (!item) { return OptionalMilestoneEntryResult::failure(item.code); }
    if (!item.value) { return OptionalMilestoneEntryResult::success(std::nullopt); }
    MilestoneEntryRecord entry;
    entry.milestoneId = milestone.id;
    entry.itemId = item.value->id;
    const auto stored = _store.latest(milestone.id, itemId, cut);
    if (!stored) { return OptionalMilestoneEntryResult::failure(stored.code); }
    if (stored.value) {
        entry.state = effectiveState(*stored.value, *item.value);
        entry.implementedCpSeq = stored.value->implementedCpSeq;
    }
    return OptionalMilestoneEntryResult::success(entry);
}

}
