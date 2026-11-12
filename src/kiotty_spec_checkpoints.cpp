
#include "kiotty_spec_adapter.h"
#include "implementation/context/kiotty_cursor_rows.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

KiottySpecStepRun convertStepRun(const impl::CursorRow& row);
KiottySpecUnmetCondition convertUnmetCondition(const impl::CursorRow& row);

}

extern "C" {

KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext* context,
                                                            KiottySpecStep step,
                                                            KiottySpecText description)
{
    KiottySpecCheckpointHandleResult result {};
    const OpenedResult spec = findOpenedContext(context);
    result.code = spec ? KIOTTY_SPEC_STATUS_INVALID_ARGUMENT : spec.code;
    if (!spec) { return result; }
    const StepResult parsed_step = ValueParser(*spec.value).parseStep(step);
    if (!parsed_step) {
        result.code = parsed_step.code;
        return result;
    }
    const TextResult parsed = Boundary(*spec.value).parse(description, Field::DESCRIPTION);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    const auto started =
        spec.value->writeSide().lifecycle().start(parsed_step.value, parsed.value);
    result.code = started.code;
    result.value = started ? started.value : 0;
    return result;
}

KiottySpecStatusResult KiottySpec_resumeCheckpoint(KiottySpecContext* context)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    return status(spec.value->writeSide().lifecycle().resume());
}

KiottySpecStatusResult KiottySpec_completeCheckpoint(KiottySpecContext* context,
                                                     KiottySpecActor actor)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const ActorResult parsed = ValueParser(*spec.value).parseActor(actor);
    if (!parsed) { return status(parsed.code); }
    return status(spec.value->writeSide().lifecycle().complete(parsed.value));
}

KiottySpecCursorResult KiottySpec_openUnmetConditionCursor(KiottySpecContext* context)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    return Boundary(*spec.value).wrap(spec.value->readSide().finder().history().unmetConditions());
}

KiottySpecStatusResult KiottySpec_fetchUnmetConditionPage(KiottySpecContext* context,
                                                          KiottySpecCursor cursor,
                                                          KiottySpecUnmetConditionPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
        cursor, impl::CursorKind::UNMET_CONDITION, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecUnmetCondition> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const auto element = elements.at(page->length);
        if (!element) { return status(element.code); }
        *element.value = convertUnmetCondition(row);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCheckpointResult KiottySpec_readCheckpoint(KiottySpecContext* context,
                                                     KiottySpecCheckpointHandle handle)
{
    KiottySpecCheckpointResult result {};
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const SequenceResult parsed = ValueParser(*spec.value).parseCheckpoint(handle);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    impl::CheckpointStore& checkpoints = spec.value->workspace().checkpoints;
    const uint32_t sequence =
        handle == KIOTTY_SPEC_CHECKPOINT_CURRENT ? checkpoints.latest() : parsed.value;
    const auto record = checkpoints.get(sequence);
    if (!record) {
        result.code = spec.value->failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND,
                                                  KIOTTY_SPEC_SUBJECT_CHECKPOINT);
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.handle = sequence;
    result.value = toCheckpointRecord(*record.value, checkpoints.describe(sequence));
    return result;
}

KiottySpecCursorResult KiottySpec_openCheckpointCursor(KiottySpecContext* context,
                                                       KiottySpecStep step)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    const bool is_valid_step = entity::isValidStep(step);
    const bool is_accepted_step = step == 0 || is_valid_step;
    if (!is_accepted_step) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return Boundary(*spec.value).wrap(spec.value->readSide().finder().history().checkpoints(step));
}

KiottySpecStatusResult KiottySpec_fetchCheckpointPage(KiottySpecContext* context,
                                                      KiottySpecCursor cursor,
                                                      KiottySpecCheckpointPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    impl::CheckpointStore& checkpoints = spec.value->workspace().checkpoints;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
        cursor, impl::CursorKind::CHECKPOINT, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecCheckpointHandle> handles(page->handles, KIOTTY_SPEC_PAGE_CAPACITY);
    const impl::Span<KiottySpecCheckpoint> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const auto record = checkpoints.get(static_cast<uint32_t>(row.number));
        if (!record) { continue; }
        const auto handle = handles.at(page->length);
        const auto element = elements.at(page->length);
        const bool has_slot = handle && element;
        if (!has_slot) { return status(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
        *handle.value = static_cast<KiottySpecCheckpointHandle>(row.number);
        const uint32_t sequence = static_cast<uint32_t>(row.number);
        *element.value = toCheckpointRecord(*record.value, checkpoints.describe(sequence));
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecSizeResult KiottySpec_pruneCheckpoints(KiottySpecContext* context, KiottySpecActor actor,
                                                 KiottySpecCheckpointHandle base)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return sizeResult(spec.code, 0); }
    const ValueParser parser(*spec.value);
    const ActorResult parsed_actor = parser.parseActor(actor);
    if (!parsed_actor) { return sizeResult(parsed_actor.code, 0); }
    const SequenceResult parsed_base = parser.parseCheckpoint(base);
    if (!parsed_base) { return sizeResult(parsed_base.code, 0); }
    const auto pruned =
        spec.value->writeSide().lifecycle().prune(parsed_actor.value, parsed_base.value);
    return sizeResult(pruned.code, pruned ? pruned.value : 0);
}

KiottySpecStatusResult KiottySpec_writeDecision(KiottySpecContext* context, KiottySpecItemKind kind,
                                                KiottySpecText text)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    const TextResult parsed = Boundary(*spec.value).parse(text, Field::DECISION);
    if (!parsed) { return status(parsed.code); }
    const KindResult parsed_kind = ValueParser(*spec.value).parseDecisionKind(kind);
    if (!parsed_kind) { return status(parsed_kind.code); }
    return status(
        spec.value->writeSide().editor().content().writeDecision(parsed_kind.value, parsed.value));
}

KiottySpecSizeResult KiottySpec_readDecision(KiottySpecContext* context, KiottySpecItemKind kind,
                                             KiottySpecBuffer buffer)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return sizeResult(spec.code, 0); }
    const KindResult parsed = ValueParser(*spec.value).parseDecisionKind(kind);
    if (!parsed) { return sizeResult(parsed.code, 0); }
    const impl::Span<char> target = toSpan(buffer);
    const auto read = spec.value->workspace().decisions.read(parsed.value, UINT32_MAX, target);
    return sizeResult(read.code, read.value);
}

KiottySpecPositionResult KiottySpec_readPosition(KiottySpecContext* context)
{
    KiottySpecPositionResult result {};
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) {
        result.code = spec.code;
        return result;
    }
    const auto position = spec.value->readSide().finder().history().position();
    result.code = position.code;
    if (!position) { return result; }
    result.value.current = position.value.current;
    result.value.step = static_cast<KiottySpecStep>(position.value.step);
    result.value.state = static_cast<KiottySpecCheckpointState>(position.value.state);
    result.value.next_step = static_cast<KiottySpecStep>(position.value.next_step);
    result.value.pending_writes = position.value.pending_writes;
    result.value.open_change_requests = position.value.open_change_requests;
    return result;
}

KiottySpecCursorResult KiottySpec_openStepRunCursor(KiottySpecContext* context)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return cursorFailure(spec.code); }
    return Boundary(*spec.value).wrap(spec.value->readSide().finder().history().stepRuns());
}

KiottySpecStatusResult KiottySpec_fetchStepRunPage(KiottySpecContext* context,
                                                   KiottySpecCursor cursor,
                                                   KiottySpecStepRunPage* page)
{
    const OpenedResult spec = findOpenedContext(context);
    if (!spec) { return status(spec.code); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = spec.value->readSide().cursors().fetch(
        cursor, impl::CursorKind::STEP_RUN, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecStepRun> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const auto element = elements.at(page->length);
        if (!element) { return status(element.code); }
        *element.value = convertStepRun(row);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}

namespace {

KiottySpecStepRun convertStepRun(const impl::CursorRow& row)
{
    KiottySpecStepRun run {};
    run.first = row.first;
    run.last = row.second;
    run.count = static_cast<uint64_t>(row.number);
    run.step = static_cast<KiottySpecStep>(row.third & LOW_MASK);
    run.state = static_cast<KiottySpecCheckpointState>(row.third >> SHIFT);
    return run;
}

KiottySpecUnmetCondition convertUnmetCondition(const impl::CursorRow& row)
{
    KiottySpecUnmetCondition unmet {};
    copyText(unmet.id, impl::unpackId(row.first, row.second));
    unmet.condition = static_cast<KiottySpecCondition>(row.third);
    return unmet;
}

}
