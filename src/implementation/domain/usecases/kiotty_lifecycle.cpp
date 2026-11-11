#include "implementation/domain/usecases/kiotty_lifecycle.h"

#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;
using SequenceResult = Result<uint32_t, KiottySpecStatus>;

}

KiottySpecStatus Lifecycle::requireUser(KiottySpecActor actor)
{
    if (actor == KIOTTY_SPEC_ACTOR_USER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _w.failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, KIOTTY_SPEC_SUBJECT_ACTOR);
}

SequenceResult Lifecycle::beginWrite()
{
    const KiottySpecStatus fresh = _w.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(fresh); }
    const uint32_t latest = _w.checkpoints.latest();
    const CheckpointRecord* record = _w.checkpoints.at(latest);
    if (record == nullptr) {
        return SequenceResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    if (record->state == CheckpointState::DONE) {
        return SequenceResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_CONFIRMED, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    if (record->state == CheckpointState::STARTED) {
        const KiottySpecStatus running =
            _w.checkpoints.writer().setState(latest, CheckpointState::RUNNING);
        if (running != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(running); }
    }
    return SequenceResult::success(latest);
}

SequenceResult Lifecycle::start(entity::Step step, Span<const char> description)
{
    const bool is_valid_step = entity::isValidStep(static_cast<uint32_t>(step));
    if (!is_valid_step) {
        return SequenceResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_STEP));
    }
    const KiottySpecStatus fresh = _w.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(fresh); }
    const auto created = _w.checkpoints.writer().create(step, description, nowEpochSeconds());
    const KiottySpecStatus finished = _w.finish(created.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(finished); }
    return created;
}

KiottySpecStatus Lifecycle::resume()
{
    const auto sequence = beginWrite();
    return _w.finish(sequence.code);
}

KiottySpecStatus Lifecycle::complete(KiottySpecActor actor)
{
    const KiottySpecStatus allowed = requireUser(actor);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return allowed; }
    const KiottySpecStatus fresh = _w.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const uint32_t latest = _w.checkpoints.latest();
    const CheckpointRecord* record = _w.checkpoints.at(latest);
    if (record == nullptr) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT);
    }
    if (record->state == CheckpointState::DONE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const Span<const UnmetCondition> unmet = _checker.collect(record->step);
    const size_t unmet_count = unmet.length();
    if (unmet_count > 0) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_COMPLETION);
    }
    return _w.finish(_w.checkpoints.writer().setState(latest, CheckpointState::DONE));
}

Result<size_t, KiottySpecStatus> Lifecycle::prune(KiottySpecActor actor, uint32_t base)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    const KiottySpecStatus allowed = requireUser(actor);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(allowed); }
    const KiottySpecStatus fresh = _w.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(fresh); }
    const CheckpointRecord* record = _w.checkpoints.at(base);
    if (record == nullptr) {
        return CountResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    if (record->state != CheckpointState::DONE) {
        return CountResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    const auto dropped = _w.checkpoints.writer().dropBefore(base);
    _w.items.history().prune(base);
    _w.relations.versions().prune(base);
    _w.decisions.versions().prune(base);
    const KiottySpecStatus finished = _w.finish(dropped.code);
    if (finished != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(finished); }
    const KiottySpecStatus reloaded = _w.reload();
    if (reloaded != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(reloaded); }
    return dropped;
}

}
