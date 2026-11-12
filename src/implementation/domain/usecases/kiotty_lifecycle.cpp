#include "implementation/domain/usecases/kiotty_lifecycle.h"

#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;
using SequenceResult = Result<uint32_t, KiottySpecStatus>;

}

SequenceResult Lifecycle::start(entity::Step step, Span<const char> description)
{
    const bool is_valid_step = entity::isValidStep(static_cast<uint32_t>(step));
    if (!is_valid_step) {
        return SequenceResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_STEP));
    }
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(fresh); }
    const SequenceResult created =
        _workspace.checkpoints.writer().create(step, description, nowEpochSeconds());
    const KiottySpecStatus finished = _workspace.finish(created.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(finished); }
    return created;
}

KiottySpecStatus Lifecycle::resume()
{
    WriteScope scope(_workspace);
    return scope.finish(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecStatus Lifecycle::complete(KiottySpecActor actor)
{
    const KiottySpecStatus allowed = requireUser(actor);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return allowed; }
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const uint32_t latest = _workspace.checkpoints.latest();
    const auto found = _workspace.checkpoints.get(latest);
    if (!found) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND,
                                        KIOTTY_SPEC_SUBJECT_CHECKPOINT);
    }
    const CheckpointRecord& record = *found.value;
    if (record.state == CheckpointState::DONE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const Span<const UnmetCondition> unmet = _checker.collect(record.step);
    const size_t unmet_count = unmet.length();
    if (unmet_count > 0) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION,
                                        KIOTTY_SPEC_SUBJECT_COMPLETION);
    }
    return _workspace.finish(
        _workspace.checkpoints.writer().setState(latest, CheckpointState::DONE));
}

Result<size_t, KiottySpecStatus> Lifecycle::prune(KiottySpecActor actor, uint32_t base)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    const KiottySpecStatus allowed = requireUser(actor);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(allowed); }
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(fresh); }
    const auto record = _workspace.checkpoints.get(base);
    if (!record) {
        return CountResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    const bool is_done = (*record.value).state == CheckpointState::DONE;
    if (!is_done) {
        return CountResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    const auto dropped = _workspace.checkpoints.writer().dropBefore(base);
    _workspace.items.maintenance().prune(base);
    _workspace.relations.versions().batch().prune(base);
    _workspace.decisions.versions().batch().prune(base);
    const KiottySpecStatus finished = _workspace.finish(dropped.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(finished); }
    const KiottySpecStatus reloaded = _workspace.reload();
    if (reloaded != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(reloaded); }
    return dropped;
}

KiottySpecStatus Lifecycle::requireUser(KiottySpecActor actor)
{
    if (actor == KIOTTY_SPEC_ACTOR_USER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _workspace.failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, KIOTTY_SPEC_SUBJECT_ACTOR);
}

}
