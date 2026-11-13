#include "implementation/domain/usecases/rule_sync/kiotty_rule_acceptor.h"

#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/remote/kiotty_remote_state.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using OutcomeResult = Result<AcceptOutcome, KiottySpecStatus>;
using HandleResult = Result<ItemHandle, KiottySpecStatus>;

constexpr uint32_t FIRST_STEP_BIT = 1;

KiottySpecStatus raiseRemoteRules(Workspace& workspace, KiottySpecStatus status);
RuleChange changeOf(RuleSide local, RuleSide remote, int order);
uint64_t bitOf(const ModelFinder& finder, uint32_t tag, std::string_view name);

}

Result<AcceptOutcome, KiottySpecStatus> RuleAcceptor::accept(Workspace& remote,
                                                             const AcceptDraft& draft,
                                                             entity::Actor actor)
{
    const KiottySpecStatus checked = checkAccept(remote, actor);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(checked); }
    const KiottySpecStatus collected = collectChanges(remote);
    if (collected != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(collected); }
    if (_change_count == 0) {
        return OutcomeResult::failure(
            raiseRemoteRules(_local, KIOTTY_SPEC_STATUS_PRECONDITION));
    }
    const OutcomeResult applied = applyAll(remote, draft);
    const KiottySpecStatus finished = _local.finish(applied.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(finished); }
    return applied;
}

KiottySpecStatus RuleAcceptor::checkAccept(Workspace& remote, entity::Actor actor)
{
    if (actor != entity::Actor::USER) {
        return _local.failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, KIOTTY_SPEC_SUBJECT_ACTOR);
    }
    const KiottySpecStatus fresh = _local.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto latest = _local.checkpoints.get(_local.checkpoints.latest());
    const bool is_done = latest && (*latest.value).state == entity::CheckpointState::DONE;
    if (!is_done) {
        return _local.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION,
                                    KIOTTY_SPEC_SUBJECT_CHECKPOINT);
    }
    const bool is_same_model = _local.file.modelVersion() == remote.file.modelVersion();
    if (!is_same_model) { return raiseRemoteRules(_local, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::collectChanges(Workspace& remote)
{
    _change_count = 0;
    RuleOrder walk {skipToRule(_local, 0), skipToRule(remote, 0)};
    const size_t local_count = _local.items.slots().count();
    const size_t remote_count = remote.items.slots().count();
    while (walk.local < local_count || walk.remote < remote_count) {
        const RuleSide local {_local, ruleAt(_local, walk.local)};
        const RuleSide other {remote, ruleAt(remote, walk.remote)};
        const int order = compareOrder(local, other, walk);
        const RuleChange change = changeOf(local, other, order);
        walk = advanceOrder(RulePair {_local, remote}, walk, order);
        if (change.change == 0) { continue; }
        if (_change_count == _changes.size()) {
            return raiseRemoteRules(_local, KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
        }
        _changes[_change_count] = change;
        ++_change_count;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<AcceptOutcome, KiottySpecStatus> RuleAcceptor::applyAll(Workspace& remote,
                                                               const AcceptDraft& draft)
{
    const auto sequence = _local.checkpoints.writer().create(
        entity::Step::CHANGE_REQUEST_REPORTED, draft.description, nowEpochSeconds());
    if (!sequence) { return OutcomeResult::failure(sequence.code); }
    const uint32_t at = sequence.value;
    KiottySpecStatus status =
        _local.checkpoints.writer().setState(at, entity::CheckpointState::RUNNING);
    const HandleResult request = createRequest(draft, at);
    if (!request) { return OutcomeResult::failure(request.code); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = copyRules(remote, at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = linkParents(remote, at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = linkRequest(request.value, at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = linkPrivateRules(request.value, at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = removeResults(at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = removeRules(at); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = RemoteState(_local).writeBase(draft.rule_base);
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(status); }
    const auto label = _local.items.reader().label(request.value);
    if (!label) { return OutcomeResult::failure(label.code); }
    return OutcomeResult::success(AcceptOutcome {(*label.value).id, at});
}

Result<ItemHandle, KiottySpecStatus> RuleAcceptor::createRequest(const AcceptDraft& draft,
                                                                 uint32_t sequence)
{
    const auto id =
        _local.items.history().nextId(_local.catalog.model(ItemKind::CHANGE_REQUEST).id_prefix.view());
    if (!id) { return HandleResult::failure(id.code); }
    entity::ItemLabel label {};
    label.id = id.value;
    const KiottySpecStatus titled = label.title.assign(draft.title);
    const KiottySpecStatus summarized = label.summary.assign(draft.summary);
    if (titled != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(titled); }
    if (summarized != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(summarized); }
    entity::ItemState state {};
    state.parent = entity::INVALID_ITEM_HANDLE;
    state.kind = ItemKind::CHANGE_REQUEST;
    const HandleResult created = _local.items.writer().create(label, state, sequence);
    if (!created) { return created; }
    KiottySpecStatus status = _local.items.writer().writeBody(created.value, draft.body, sequence);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = tagRequest(created.value, draft.entry_step, sequence);
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(status); }
    return created;
}

KiottySpecStatus RuleAcceptor::tagRequest(ItemHandle request, entity::Step entry_step,
                                          uint32_t sequence)
{
    const ModelFinder& finder = _local.catalog.finder();
    const ItemKind kind = ItemKind::CHANGE_REQUEST;
    const auto source = finder.findTag(kind, spanOf(CHANGE_REQUEST_SOURCE_TAG));
    const auto state = finder.findTag(kind, spanOf(CHANGE_REQUEST_STATE_TAG));
    const auto entry = finder.findTag(kind, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    if (!source || !state || !entry) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    const uint32_t step_bit = static_cast<uint32_t>(entry_step) - FIRST_STEP_BIT;
    ItemWriter writer = _local.items.writer();
    KiottySpecStatus status = writer.setTag(
        request, source.value, bitOf(finder, source.value, COMMON_RULE_SOURCE_BIT), sequence);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = writer.setTag(request, state.value,
                               bitOf(finder, state.value, CHANGE_REQUEST_OPEN_BIT), sequence);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = writer.setTag(request, entry.value, uint64_t{1} << step_bit, sequence);
    }
    return status;
}

namespace {

KiottySpecStatus raiseRemoteRules(Workspace& workspace, KiottySpecStatus status)
{
    return workspace.failure.raise(status, KIOTTY_SPEC_SUBJECT_REMOTE_RULES);
}

RuleChange changeOf(RuleSide local, RuleSide remote, int order)
{
    RuleChange change {local.handle, remote.handle, 0, 0};
    if (order < 0) { change.change = KIOTTY_SPEC_CHANGE_REMOVED; }
    if (order > 0) { change.change = KIOTTY_SPEC_CHANGE_ADDED; }
    if (order != 0) { return change; }
    change.fields = compareRules(local, remote);
    change.change = change.fields != 0 ? KIOTTY_SPEC_CHANGE_MODIFIED : 0;
    return change;
}

uint64_t bitOf(const ModelFinder& finder, uint32_t tag, std::string_view name)
{
    const auto bit = finder.findTagBit(ItemKind::CHANGE_REQUEST, tag, spanOf(name));
    return bit ? uint64_t{1} << bit.value : 0;
}

}

}
