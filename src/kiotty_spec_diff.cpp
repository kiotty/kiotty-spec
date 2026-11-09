// C 인터페이스: 체크포인트 diff · 시점 읽기 · 글자 diff. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecDiffSummaryResult KiottySpec_summarizeDiff(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    KiottySpecDiffSummaryResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto range = differ.range(query.from, query.to);
    if (!range) {
        result.code = range.code;
        return result;
    }
    impl::KindCounts counts[entity::ITEM_KIND_COUNT] {};
    result.code = differ.summarize(range.value, query.kind_mask, query.field_mask, counts);
    result.from = range.value.from;
    result.to = range.value.to;
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT; ++index) {
        const impl::KindCounts& count = counts[index];
        result.kinds[index] = KiottySpecKindDiff{count.added, count.modified, count.removed, count.relations_added, count.relations_removed, count.decision_changed};
    }
    return result;
}

KiottySpecCursorResult KiottySpec_openItemDiffCursor(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto range = context->context.differ().range(query.from, query.to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, context->context.differ().items(range.value, query.kind_mask, query.field_mask));
}

KiottySpecStatusResult KiottySpec_fetchItemDiffPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecItemDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::ITEM_DIFF, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        const impl::ItemDiffRow diff = impl::unpackItemDiff(row);
        KiottySpecItemDiff& element = page->elements[index];
        element = KiottySpecItemDiff{};
        element.before = diff.before;
        element.after = diff.after;
        const auto handle = items.reader().find(diff.id.view());
        element.item = handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
        element.kind = static_cast<KiottySpecItemKind>(diff.kind);
        element.change = static_cast<KiottySpecChange>(diff.change);
        element.changed_fields = diff.fields;
        copyText(element.id, diff.id);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openRelationDiffCursor(KiottySpecContext* context, KiottySpecDiffQuery query)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    const auto range = context->context.differ().range(query.from, query.to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, context->context.differ().relations(range.value, query.kind_mask));
}

KiottySpecStatusResult KiottySpec_fetchRelationDiffPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecRelationDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::RELATION_DIFF, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        const impl::RelationDiffRow diff = impl::unpackRelationDiff(row);
        KiottySpecRelationDiff& element = page->elements[index];
        element = KiottySpecRelationDiff{};
        element.checkpoint = diff.checkpoint;
        element.model = static_cast<KiottySpecItemKind>(diff.model);
        element.relation = diff.relation;
        element.change = static_cast<KiottySpecChange>(diff.change);
        copyText(element.from_id, diff.from_id);
        copyText(element.to_id, diff.to_id);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecItemResult KiottySpec_readItemAt(KiottySpecContext* context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecItemResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(spanOf(id), point.value);
    result.code = version.code;
    if (!version) { return result; }
    const auto handle = context->context.workspace().items.reader().find(spanOf(id));
    result.handle = handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
    result.value = toItem(version.value.record);
    return result;
}

KiottySpecTagResult KiottySpec_readItemTagAt(KiottySpecContext* context, KiottySpecText id, uint32_t tag, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecTagResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(spanOf(id), point.value);
    if (!version) {
        result.code = version.code;
        return result;
    }
    if (tag >= context->context.catalog().model(version.value.record.kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("모델에 없는 태그 index")});
        return result;
    }
    const auto value = differ.past().tagAt(spanOf(id), tag, point.value);
    result.code = value.code;
    const auto handle = context->context.workspace().items.reader().find(spanOf(id));
    result.value = KiottySpecTag{handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE, static_cast<KiottySpecItemKind>(version.value.record.kind),
                                 tag, value ? value.value : 0};
    return result;
}

KiottySpecSizeResult KiottySpec_readItemBodyAt(KiottySpecContext* context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint, char* buffer, size_t capacity)
{
    if (context == nullptr) { return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0}; }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return KiottySpecSizeResult{point.code, 0}; }
    const auto read = differ.past().bodyAt(spanOf(id), point.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecSizeResult KiottySpec_readDecisionAt(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecCheckpointHandle checkpoint, char* buffer, size_t capacity)
{
    if (context == nullptr || !isKind(kind) || !entity::hasDecision(static_cast<entity::ItemKind>(kind))) {
        return KiottySpecSizeResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0};
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return KiottySpecSizeResult{point.code, 0}; }
    const auto read = differ.past().decisionAt(static_cast<entity::ItemKind>(kind), point.value, buffer, capacity);
    return KiottySpecSizeResult{read.code, read.value};
}

KiottySpecCursorResult KiottySpec_openTextDiffCursor(KiottySpecContext* context, KiottySpecItemHandle item, KiottySpecTextField field,
                                                     KiottySpecCheckpointHandle from, KiottySpecCheckpointHandle to)
{
    if (context == nullptr) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0, 0}; }
    impl::Differ& differ = context->context.differ();
    if (field != KIOTTY_SPEC_TEXT_FIELD_TITLE && field != KIOTTY_SPEC_TEXT_FIELD_SUMMARY) {
        return KiottySpecCursorResult{context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {impl::literalText("글자 diff 는 제목이나 요약 하나에만 쓴다")}), 0, 0};
    }
    if (!context->context.workspace().items.reader().get(fromHandle(item))) { return KiottySpecCursorResult{KIOTTY_SPEC_STATUS_INVALID_HANDLE, 0, 0}; }
    const auto range = differ.range(from, to);
    if (!range) { return KiottySpecCursorResult{range.code, 0, 0}; }
    return cursorResult(context->context, differ.text().open(fromHandle(item), static_cast<uint32_t>(field), range.value));
}

KiottySpecTextDiffResult KiottySpec_nextTextDiff(KiottySpecContext* context, KiottySpecCursor cursor)
{
    KiottySpecTextDiffResult result {};
    if (context == nullptr) {
        result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        return result;
    }
    const auto piece = context->context.differ().text().next(cursor);
    result.code = piece.code;
    if (piece) { result.value = KiottySpecTextDiff{piece.value.text, piece.value.length, static_cast<KiottySpecTextChange>(piece.value.change), 0}; }
    return result;
}

KiottySpecStatusResult KiottySpec_closeTextDiffCursor(KiottySpecContext* context, KiottySpecCursor cursor) { return KiottySpec_closeCursor(context, cursor); }

}
