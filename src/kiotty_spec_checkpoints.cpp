
#include "kiotty_spec_adapter.h"
#include "implementation/context/kiotty_cursor_rows.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

KiottySpecStepRun convertStepRun(const impl::CursorRow& row);
KiottySpecUnmetCondition convertUnmetCondition(const impl::CursorRow& row);
bool hasDecisionKind(KiottySpecItemKind kind);

}

extern "C" {

KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext* context,
                                                            KiottySpecStep step,
                                                            KiottySpecText description)
{
    KiottySpecCheckpointHandleResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const TextResult parsed = parseDescription(context->context, description);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    const auto started =
        context->context.lifecycle().start(static_cast<entity::Step>(step), parsed.value);
    result.code = started.code;
    result.value = started ? started.value : 0;
    return result;
}

KiottySpecStatusResult KiottySpec_resumeCheckpoint(KiottySpecContext* context)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().resume());
}

KiottySpecStatusResult KiottySpec_completeCheckpoint(KiottySpecContext* context,
                                                     KiottySpecActor actor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().complete(actor));
}

KiottySpecCursorResult KiottySpec_openUnmetConditionCursor(KiottySpecContext* context)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return cursorResult(context->context, context->context.finder().history().unmetConditions());
}

KiottySpecStatusResult KiottySpec_fetchUnmetConditionPage(KiottySpecContext* context,
                                                          KiottySpecCursor cursor,
                                                          KiottySpecUnmetConditionPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::UNMET_CONDITION,
                                                       KIOTTY_SPEC_PAGE_CAPACITY);
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
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    const uint32_t sequence = handle == KIOTTY_SPEC_CHECKPOINT_CURRENT
                                  ? checkpoints.latest()
                                  : static_cast<uint32_t>(handle & LOW_MASK);
    const auto record = checkpoints.get(sequence);
    if (!record) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND,
                                                       KIOTTY_SPEC_SUBJECT_CHECKPOINT);
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.handle = sequence;
    result.value = toCheckpointRecord(*record.value);
    return result;
}

KiottySpecCursorResult KiottySpec_openCheckpointCursor(KiottySpecContext* context,
                                                       KiottySpecStep step)
{
    const bool is_valid_step = entity::isValidStep(step);
    const bool is_accepted_step = step == 0 || is_valid_step;
    const bool has_valid_arguments = context != nullptr && is_accepted_step;
    if (!has_valid_arguments) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return cursorResult(context->context, context->context.finder().history().checkpoints(step));
}

KiottySpecStatusResult KiottySpec_fetchCheckpointPage(KiottySpecContext* context,
                                                      KiottySpecCursor cursor,
                                                      KiottySpecCheckpointPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::CHECKPOINT,
                                                       KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    const impl::Span<KiottySpecCheckpointHandle> handles(page->handles, KIOTTY_SPEC_PAGE_CAPACITY);
    const impl::Span<KiottySpecCheckpoint> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const entity::CheckpointRecord* record = checkpoints.at(static_cast<uint32_t>(row.number));
        if (record == nullptr) { continue; }
        const auto handle = handles.at(page->length);
        const auto element = elements.at(page->length);
        const bool has_slot = handle && element;
        if (!has_slot) { return status(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
        *handle.value = static_cast<KiottySpecCheckpointHandle>(row.number);
        *element.value = toCheckpointRecord(*record);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecSizeResult KiottySpec_pruneCheckpoints(KiottySpecContext* context, KiottySpecActor actor,
                                                 KiottySpecCheckpointHandle base)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const auto pruned =
        context->context.lifecycle().prune(actor, static_cast<uint32_t>(base & LOW_MASK));
    return sizeResult(pruned.code, pruned ? pruned.value : 0);
}

KiottySpecStatusResult KiottySpec_writeDecision(KiottySpecContext* context, KiottySpecItemKind kind,
                                                KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult parsed = parseDecision(context->context, text);
    if (!parsed) { return status(parsed.code); }
    return status(
        context->context.editor().writeDecision(static_cast<entity::ItemKind>(kind), parsed.value));
}

KiottySpecSizeResult KiottySpec_readDecision(KiottySpecContext* context, KiottySpecItemKind kind,
                                             KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const bool has_decision = hasDecisionKind(kind);
    if (!has_decision) {
        return sizeResult(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                           KIOTTY_SPEC_SUBJECT_KIND),
                          0);
    }
    const auto read = context->context.workspace().decisions.read(
        static_cast<entity::ItemKind>(kind), UINT32_MAX, buffer.data, buffer.capacity);
    return sizeResult(read.code, read.value);
}

KiottySpecModelResult KiottySpec_readModel(KiottySpecContext* context, KiottySpecItemKind kind)
{
    KiottySpecModelResult result {};
    const bool is_valid_kind = isKind(kind);
    const bool has_valid_arguments = context != nullptr && is_valid_kind;
    if (!has_valid_arguments) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model =
        context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, model.name);
    copyText(result.value.id_prefix, model.id_prefix);
    result.value.kind = kind;
    result.value.created_in = static_cast<KiottySpecStep>(model.created_in);
    result.value.tag_count = static_cast<uint32_t>(model.tags.length());
    result.value.relation_count = static_cast<uint32_t>(model.relations.length());
    return result;
}

KiottySpecStatusResult KiottySpec_readTagDefinition(KiottySpecContext* context,
                                                    KiottySpecItemKind kind, uint32_t index,
                                                    KiottySpecTagDefinition* definition)
{
    const bool is_valid_kind = isKind(kind);
    const bool has_valid_arguments = context != nullptr && definition != nullptr && is_valid_kind;
    if (!has_valid_arguments) {
        return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const entity::Model& model =
        context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    const auto source = model.tags.at(index);
    if (!source) { return status(source.code); }
    copyText(definition->name, source.value->name);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        copyText(definition->bit_names[bit], source.value->bit_names[bit]);
    }
    definition->cardinality = static_cast<KiottySpecCardinality>(source.value->cardinality);
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecRelationDefinitionResult KiottySpec_readRelationDefinition(KiottySpecContext* context,
                                                                     KiottySpecItemKind kind,
                                                                     uint32_t index)
{
    KiottySpecRelationDefinitionResult result {};
    const bool is_valid_kind = isKind(kind);
    const bool has_valid_arguments = context != nullptr && is_valid_kind;
    if (!has_valid_arguments) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model =
        context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    const auto definition = model.relations.at(index);
    if (!definition) {
        result.code = definition.code;
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, definition.value->name);
    result.value.target_kind = static_cast<KiottySpecItemKind>(definition.value->target_kind);
    result.value.cardinality = static_cast<KiottySpecCardinality>(definition.value->cardinality);
    return result;
}

KiottySpecPositionResult KiottySpec_readPosition(KiottySpecContext* context)
{
    KiottySpecPositionResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto position = context->context.finder().history().position();
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
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return cursorResult(context->context, context->context.finder().history().stepRuns());
}

KiottySpecStatusResult KiottySpec_fetchStepRunPage(KiottySpecContext* context,
                                                   KiottySpecCursor cursor,
                                                   KiottySpecStepRunPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::STEP_RUN,
                                                       KIOTTY_SPEC_PAGE_CAPACITY);
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

bool hasDecisionKind(KiottySpecItemKind kind)
{ return isKind(kind) && entity::hasDecision(static_cast<entity::ItemKind>(kind)); }

}
