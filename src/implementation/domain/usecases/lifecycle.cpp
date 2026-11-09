#include "implementation/domain/usecases/lifecycle.h"

#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/domain/entity/names.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr int32_t RELEASE_STEP = 10;

}

KiottySpecStatus Lifecycle::requireUser(KiottySpecActor actor, Span<const char> what)
{
    if (actor == KIOTTY_SPEC_ACTOR_USER) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _s.failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, {what, literalText(": 사용자만 할 수 있음 (R-2)")});
}

KiottySpecStatus Lifecycle::rejectVerdict(Span<const char> heading, const Verdict &verdict)
{
    return _s.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {heading, literalText(":\n"), spanOf(verdict.report)});
}

SequenceResult Lifecycle::confirm(KiottySpecActor actor, std::string_view cpId)
{
    const KiottySpecStatus allowed = requireUser(actor, literalText("체크포인트 확정"));
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(allowed); }
    const auto seq = _s.checkpoints.resolve(cpId);
    if (!seq) { return seq; }
    if (seq.value == kPending) {
        return SequenceResult::failure(_s.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                        {literalText("PENDING 은 확정할 수 없음. 먼저 체크포인트를 찍을 것")}));
    }
    const auto record = _s.checkpoints.get(seq.value);
    if (!record) { return SequenceResult::failure(record.code); }
    if (record.value.confirmed) { return seq; }
    const auto verdict = _s.checker.confirmCheck(record.value.step, seq.value);
    if (!verdict) { return SequenceResult::failure(verdict.code); }
    if (!verdict.value.ok) {
        const std::string heading = "Step " + std::to_string(record.value.step) + " 확정 조건 미충족";
        return SequenceResult::failure(rejectVerdict(spanOf(heading), verdict.value));
    }
    const KiottySpecStatus marked = _s.checkpoints.markConfirmed(seq.value);
    if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(marked); }
    return seq;
}

SequenceResult Lifecycle::release(KiottySpecActor actor, std::string_view note)
{
    const KiottySpecStatus allowed = requireUser(actor, literalText("RELEASE"));
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(allowed); }
    const auto verdict = _s.checker.releaseCheck();
    if (!verdict) { return SequenceResult::failure(verdict.code); }
    if (!verdict.value.ok) { return SequenceResult::failure(rejectVerdict(literalText("RELEASE 조건 미충족 (R-10)"), verdict.value)); }
    Savepoint savepoint(_s.storage.connection());
    const KiottySpecStatus begun = _s.storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(begun); }
    const auto seq = _s.checkpoints.create(RELEASE_STEP, note, KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE);
    if (!seq) { return seq; }
    const KiottySpecStatus marked = _s.checkpoints.markConfirmed(seq.value);
    if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(marked); }
    const KiottySpecStatus committed = _s.storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(committed); }
    return seq;
}

CountResult Lifecycle::prune(KiottySpecActor actor, std::string_view baseCpId)
{
    const KiottySpecStatus allowed = requireUser(actor, literalText("체크포인트 정리"));
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(allowed); }
    const auto base = _s.checkpoints.resolve(baseCpId);
    if (!base) { return CountResult::failure(base.code); }
    if (base.value == kPending) {
        return CountResult::failure(_s.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("PENDING 은 기준이 될 수 없음")}));
    }
    const auto record = _s.checkpoints.get(base.value);
    if (!record) { return CountResult::failure(record.code); }
    if (!record.value.confirmed) {
        return CountResult::failure(_s.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("기준 체크포인트는 확정된 것이어야 함")}));
    }
    const auto all = _s.checkpoints.list(0);
    if (!all) { return CountResult::failure(all.code); }
    for (const auto &checkpoint : all.value) {
        if (checkpoint.seq < base.value && checkpoint.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE) {
            const std::string name = checkpointName(checkpoint.seq);
            return CountResult::failure(_s.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION,
                                                         {literalText("RELEASE 체크포인트 "), spanOf(name),
                                                          literalText(" 는 지울 수 없음. 그 이후를 기준으로 할 것")}));
        }
    }
    return _s.history.prune(base.value);
}

KiottySpecStatus Lifecycle::removeItem(std::string_view itemId)
{
    Savepoint savepoint(_s.storage.connection());
    const KiottySpecStatus begun = _s.storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return begun; }
    const auto found = _s.items.get(itemId);
    if (!found) { return found.code; }
    const KiottySpecStatus unlinked = _s.links.removeAllFor(itemId);
    if (unlinked != KIOTTY_SPEC_STATUS_SUCCESS) { return unlinked; }
    const KiottySpecStatus removed = _s.items.remove(itemId);
    if (removed != KIOTTY_SPEC_STATUS_SUCCESS) { return removed; }
    return _s.storage.check(savepoint.commit());
}

}
