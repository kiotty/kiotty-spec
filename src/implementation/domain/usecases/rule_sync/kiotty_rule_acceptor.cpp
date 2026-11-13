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
using CountResult = Result<size_t, KiottySpecStatus>;
using IndexResult = Result<uint32_t, KiottySpecStatus>;
using IdResult = Result<entity::IdText, KiottySpecStatus>;
using SequenceResult = Result<uint32_t, KiottySpecStatus>;

constexpr uint32_t FIRST_STEP_BIT = 1;

KiottySpecStatus raiseRemoteRules(const Workspace& workspace, KiottySpecStatus status);
RuleChange changeOf(RuleSide local, RuleSide remote, int order);
CountResult appendChange(Span<RuleChange> changes, size_t count, const RuleChange& change);
uint64_t bitOf(const ModelFinder& finder, uint32_t tag, std::string_view name);

}

Result<AcceptOutcome, KiottySpecStatus> RuleAcceptor::accept(const Workspace& remote,
                                                             const AcceptDraft& draft,
                                                             entity::Actor actor)
{
    const KiottySpecStatus checked = checkAccept(remote, actor);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(checked); }
    const KiottySpecStatus collected = collectChanges(remote);
    if (collected != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(collected); }
    const bool has_changes = changes().length() > 0;
    if (!has_changes) {
        return OutcomeResult::failure(raiseRemoteRules(_local, KIOTTY_SPEC_STATUS_PRECONDITION));
    }
    const OutcomeResult applied = applyAll(remote, draft);
    const KiottySpecStatus finished = _local.finish(applied.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return OutcomeResult::failure(finished); }
    return applied;
}

KiottySpecStatus RuleAcceptor::checkAccept(const Workspace& remote, entity::Actor actor)
{
    const bool is_user = actor == entity::Actor::USER;
    if (!is_user) {
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

KiottySpecStatus RuleAcceptor::collectChanges(const Workspace& remote)
{
    _change_count = 0;
    const RulePair pair {_local, remote};
    const size_t total = _local.items.slots().count() + remote.items.slots().count();
    RuleOrder walk {skipToRule(_local, 0), skipToRule(remote, 0)};
    for (size_t step = 0; step < total; ++step) {
        const RuleSide local {_local, ruleOf(_local, walk.local)};
        const RuleSide other {remote, ruleOf(remote, walk.remote)};
        const bool is_done = idOf(local).empty() && idOf(other).empty();
        if (is_done) { break; }
        const int order = compareOrder(local, other, walk);
        walk = advanceOrder(pair, walk, order);
        const CountResult appended =
            appendChange(viewVector(_changes), _change_count, changeOf(local, other, order));
        if (!appended) { return raiseRemoteRules(_local, appended.code); }
        _change_count = appended.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<AcceptOutcome, KiottySpecStatus> RuleAcceptor::applyAll(const Workspace& remote,
                                                               const AcceptDraft& draft)
{
    const SequenceResult sequence = _local.checkpoints.writer().create(
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
    const Result<const entity::ItemLabel&, KiottySpecStatus> label =
        _local.items.reader().label(request.value);
    if (!label) { return OutcomeResult::failure(label.code); }
    return OutcomeResult::success(AcceptOutcome {(*label.value).id, at});
}

Result<ItemHandle, KiottySpecStatus> RuleAcceptor::createRequest(const AcceptDraft& draft,
                                                                 uint32_t sequence)
{
    const entity::Model& model = _local.catalog.model(ItemKind::CHANGE_REQUEST);
    const IdResult id = _local.items.history().nextId(model.id_prefix.view());
    if (!id) { return HandleResult::failure(id.code); }
    entity::ItemLabel label {};
    label.id = id.value;
    const KiottySpecStatus titled = label.title.assign(draft.title);
    const KiottySpecStatus summarized = label.summary.assign(draft.summary);
    const bool is_assigned =
        titled == KIOTTY_SPEC_STATUS_SUCCESS && summarized == KIOTTY_SPEC_STATUS_SUCCESS;
    if (!is_assigned) { return HandleResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
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
    const IndexResult source = finder.findTag(kind, spanOf(CHANGE_REQUEST_SOURCE_TAG));
    const IndexResult state = finder.findTag(kind, spanOf(CHANGE_REQUEST_STATE_TAG));
    const IndexResult entry = finder.findTag(kind, spanOf(CHANGE_REQUEST_ENTRY_STEP_TAG));
    const bool has_tags = source && state && entry;
    if (!has_tags) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    const uint32_t step_bit = static_cast<uint32_t>(entry_step) - FIRST_STEP_BIT;
    const uint64_t source_bit = bitOf(finder, source.value, COMMON_RULE_SOURCE_BIT);
    const uint64_t open_bit = bitOf(finder, state.value, CHANGE_REQUEST_OPEN_BIT);
    ItemWriter writer = _local.items.writer();
    KiottySpecStatus status = writer.setTag(request, source.value, source_bit, sequence);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = writer.setTag(request, state.value, open_bit, sequence);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = writer.setTag(request, entry.value, uint64_t{1} << step_bit, sequence);
    }
    return status;
}

namespace {

KiottySpecStatus raiseRemoteRules(const Workspace& workspace, KiottySpecStatus status)
{
    return workspace.failure.raise(status, KIOTTY_SPEC_SUBJECT_REMOTE_RULES);
}

RuleChange changeOf(RuleSide local, RuleSide remote, int order)
{
    RuleChange change {local.handle, remote.handle, 0, 0};
    const bool is_removed = order < 0;
    const bool is_added = order > 0;
    if (is_removed) { change.change = KIOTTY_SPEC_CHANGE_REMOVED; }
    if (is_added) { change.change = KIOTTY_SPEC_CHANGE_ADDED; }
    const bool is_same_id = order == 0;
    if (!is_same_id) { return change; }
    change.fields = compareRules(local, remote);
    const bool is_modified = change.fields != 0;
    change.change = is_modified ? static_cast<uint32_t>(KIOTTY_SPEC_CHANGE_MODIFIED) : 0u;
    return change;
}

CountResult appendChange(Span<RuleChange> changes, size_t count, const RuleChange& change)
{
    const bool is_change = change.change != 0;
    if (!is_change) { return CountResult::success(count); }
    const Result<RuleChange&, KiottySpecStatus> slot = changes.at(count);
    if (!slot) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    *slot.value = change;
    return CountResult::success(count + 1);
}

uint64_t bitOf(const ModelFinder& finder, uint32_t tag, std::string_view name)
{
    const IndexResult bit = finder.findTagBit(ItemKind::CHANGE_REQUEST, tag, spanOf(name));
    return bit ? uint64_t{1} << bit.value : 0;
}

}

}
