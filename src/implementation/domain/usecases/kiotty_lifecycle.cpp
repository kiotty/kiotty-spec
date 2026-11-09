#include "implementation/domain/usecases/kiotty_lifecycle.h"

#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_savepoint.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;
using SequenceResult = Result<uint32_t, KiottySpecStatus>;

}

KiottySpecStatus Lifecycle::requireUser(KiottySpecActor actor, Span<const char> what)
{
    if (actor == KIOTTY_SPEC_ACTOR_USER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _w.failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, {what, literalText(": 사용자만 할 수 있음 (R-2)")});
}

SequenceResult Lifecycle::beginWrite()
{
    const uint32_t latest = _w.checkpoints.latest();
    const CheckpointRecord* record = _w.checkpoints.at(latest);
    if (record == nullptr) {
        return SequenceResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("체크포인트가 없음. 먼저 체크포인트를 시작할 것")}));
    }
    if (record->state == CheckpointState::DONE) {
        return SequenceResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_CONFIRMED, {literalText("가장 최근 체크포인트가 DONE 임. 새 체크포인트를 시작할 것")}));
    }
    if (record->state == CheckpointState::STARTED) {
        const KiottySpecStatus running = _w.checkpoints.writer().setState(latest, CheckpointState::RUNNING);
        if (running != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(running); }
    }
    return SequenceResult::success(latest);
}

SequenceResult Lifecycle::start(entity::Step step, Span<const char> description)
{
    if (!entity::isValidStep(static_cast<uint32_t>(step))) {
        return SequenceResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("단계는 1 ~ 12")}));
    }
    return _w.checkpoints.writer().create(step, description, nowEpochSeconds());
}

KiottySpecStatus Lifecycle::resume()
{
    const auto sequence = beginWrite();
    return sequence.code;
}

KiottySpecStatus Lifecycle::complete(KiottySpecActor actor)
{
    const KiottySpecStatus allowed = requireUser(actor, literalText("체크포인트 완료"));
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return allowed; }
    const uint32_t latest = _w.checkpoints.latest();
    const CheckpointRecord* record = _w.checkpoints.at(latest);
    if (record == nullptr) { return _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("체크포인트가 없음")}); }
    if (record->state == CheckpointState::DONE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const KiottySpecStatus checked = _checker.check(record->step);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    return _w.checkpoints.writer().setState(latest, CheckpointState::DONE);
}

Result<size_t, KiottySpecStatus> Lifecycle::prune(KiottySpecActor actor, uint32_t base)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    const KiottySpecStatus allowed = requireUser(actor, literalText("체크포인트 정리"));
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(allowed); }
    const CheckpointRecord* record = _w.checkpoints.at(base);
    if (record == nullptr) { return CountResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("기준 체크포인트가 없음")})); }
    if (record->state != CheckpointState::DONE) {
        return CountResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("기준 체크포인트는 DONE 이어야 함")}));
    }
    Savepoint savepoint(_w.storage.connection());
    const KiottySpecStatus begun = _w.storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(begun); }
    const KiottySpecStatus pruned = pruneVersions(_w.storage, base);
    if (pruned != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(pruned); }
    const auto dropped = _w.checkpoints.writer().dropBefore(base);
    if (!dropped) { return dropped; }
    const KiottySpecStatus committed = _w.storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(committed); }
    const KiottySpecStatus reloaded = _w.reload();
    if (reloaded != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(reloaded); }
    return dropped;
}

}
