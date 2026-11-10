// C 인터페이스: 체크포인트 · 결정 기록 · 모델 · 위치. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext* context, KiottySpecStep step, KiottySpecText description)
{
    if (context == nullptr) { return KiottySpecCheckpointHandleResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto started = context->context.lifecycle().start(static_cast<entity::Step>(step), spanOf(description));
    return KiottySpecCheckpointHandleResult{started.code, started ? started.value : 0};
}

KiottySpecStatusResult KiottySpec_resumeCheckpoint(KiottySpecContext* context)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().resume());
}

KiottySpecStatusResult KiottySpec_completeCheckpoint(KiottySpecContext* context, KiottySpecActor actor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.lifecycle().complete(actor));
}

KiottySpecCheckpointResult KiottySpec_readCheckpoint(KiottySpecContext* context, KiottySpecCheckpointHandle handle)
{
    KiottySpecCheckpointResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    const uint32_t sequence = handle == KIOTTY_SPEC_CHECKPOINT_CURRENT ? checkpoints.latest() : static_cast<uint32_t>(handle & 0xFFFFFFFFu);
    const auto record = checkpoints.get(sequence);
    if (!record) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {impl::literalText("없는 체크포인트")});
        return result;
    }
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    result.handle = sequence;
    result.value = toCheckpointRecord(*record.value);
    return result;
}

KiottySpecCursorResult KiottySpec_openCheckpointCursor(KiottySpecContext* context, KiottySpecStep step)
{
    if (context == nullptr || (step != 0 && !entity::isValidStep(step))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    return cursorResult(context->context, context->context.finder().history().checkpoints(step));
}

KiottySpecStatusResult KiottySpec_fetchCheckpointPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecCheckpointPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::CheckpointStore& checkpoints = context->context.workspace().checkpoints;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::CHECKPOINT, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        const entity::CheckpointRecord* record = checkpoints.at(static_cast<uint32_t>(row.number));
        if (record == nullptr) { continue; }
        page->handles[index] = static_cast<KiottySpecCheckpointHandle>(row.number);
        page->elements[index] = toCheckpointRecord(*record);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecSizeResult KiottySpec_pruneCheckpoints(KiottySpecContext* context, KiottySpecActor actor, KiottySpecCheckpointHandle base)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    const auto pruned = context->context.lifecycle().prune(actor, static_cast<uint32_t>(base & 0xFFFFFFFFu));
    return KiottySpecSizeResult{pruned.code, pruned ? pruned.value : 0};
}

KiottySpecStatusResult KiottySpec_writeDecision(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText text)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.editor().writeDecision(static_cast<entity::ItemKind>(kind), spanOf(text)));
}

KiottySpecSizeResult KiottySpec_readDecision(KiottySpecContext* context, KiottySpecItemKind kind, char* buffer, size_t capacity)
{
    if (context == nullptr || !isKind(kind) || !entity::hasDecision(static_cast<entity::ItemKind>(kind))) {
        return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0};
    }
    const auto read = context->context.workspace().decisions.read(static_cast<entity::ItemKind>(kind), UINT32_MAX, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecModelResult KiottySpec_readModel(KiottySpecContext* context, KiottySpecItemKind kind)
{
    KiottySpecModelResult result {};
    if (context == nullptr || !isKind(kind)) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, model.name);
    copyText(result.value.id_prefix, model.id_prefix);
    result.value.kind = kind;
    result.value.created_in = static_cast<KiottySpecStep>(model.created_in);
    result.value.tag_count = static_cast<uint32_t>(model.tags.length());
    result.value.relation_count = static_cast<uint32_t>(model.relations.length());
    return result;
}

KiottySpecStatusResult KiottySpec_readTagDefinition(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t index, KiottySpecTagDefinition* out)
{
    if (context == nullptr || out == nullptr || !isKind(kind)) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    if (index >= model.tags.length()) { return status(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
    const entity::TagDefinition& definition = model.tags[index];
    copyText(out->name, definition.name);
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) { copyText(out->bit_names[bit], definition.bit_names[bit]); }
    out->cardinality = static_cast<KiottySpecCardinality>(definition.cardinality);
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecRelationDefinitionResult KiottySpec_readRelationDefinition(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t index)
{
    KiottySpecRelationDefinitionResult result {};
    if (context == nullptr || !isKind(kind)) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const entity::Model& model = context->context.catalog().model(static_cast<entity::ItemKind>(kind));
    if (index >= model.relations.length()) {
        result.code = KIOTTY_SPEC_STATUS_OUT_OF_RANGE;
        return result;
    }
    const entity::RelationDefinition& definition = model.relations[index];
    result.code = KIOTTY_SPEC_STATUS_SUCCESS;
    copyText(result.value.name, definition.name);
    result.value.target_kind = static_cast<KiottySpecItemKind>(definition.target_kind);
    result.value.cardinality = static_cast<KiottySpecCardinality>(definition.cardinality);
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
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    return cursorResult(context->context, context->context.finder().history().stepRuns());
}

KiottySpecStatusResult KiottySpec_fetchStepRunPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecStepRunPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::STEP_RUN, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        page->elements[index] = KiottySpecStepRun{row.first, row.second, static_cast<KiottySpecStep>(row.third & 0xFFFFFFFFu),
                                                  static_cast<KiottySpecCheckpointState>(row.third >> SHIFT), static_cast<uint64_t>(row.number)};
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}
