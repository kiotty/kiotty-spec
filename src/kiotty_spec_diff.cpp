
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

KiottySpecKindDiff toKindDiff(const impl::KindCounts& count)
{
    KiottySpecKindDiff kind {};
    kind.added = count.added;
    kind.modified = count.modified;
    kind.removed = count.removed;
    kind.relations_added = count.relations_added;
    kind.relations_removed = count.relations_removed;
    kind.decision_changed = count.has_decision_change ? KIOTTY_SPEC_TRUE : KIOTTY_SPEC_FALSE;
    return kind;
}

KiottySpecItemHandle currentHandle(impl::Context& context, impl::Span<const char> id)
{
    const auto handle = context.workspace().items.reader().find(id);
    return handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
}

bool isTextField(KiottySpecTextField field)
{ return field == KIOTTY_SPEC_TEXT_FIELD_TITLE || field == KIOTTY_SPEC_TEXT_FIELD_SUMMARY; }
}

extern "C" {

KiottySpecDiffSummaryResult KiottySpec_summarizeDiff(KiottySpecContext* context,
                                                     const KiottySpecDiffQuery* query)
{
    KiottySpecDiffSummaryResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr || query == nullptr) { return result; }
    impl::Differ& differ = context->context.differ();
    const auto range = differ.range(query->from, query->to);
    if (!range) {
        result.code = range.code;
        return result;
    }
    impl::KindCounts counts[entity::ITEM_KIND_COUNT] {};
    result.code = differ.summarize(range.value, query->kind_mask, query->field_mask, counts);
    result.from = range.value.from;
    result.to = range.value.to;
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT; ++index) {
        result.kinds[index] = toKindDiff(counts[index]);
    }
    return result;
}

KiottySpecCursorResult KiottySpec_openItemDiffCursor(KiottySpecContext* context,
                                                     const KiottySpecDiffQuery* query)
{
    if (context == nullptr || query == nullptr) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const auto range = context->context.differ().range(query->from, query->to);
    if (!range) { return cursorFailure(range.code); }
    return cursorResult(context->context, context->context.differ().items(
                                              range.value, query->kind_mask, query->field_mask));
}

KiottySpecStatusResult KiottySpec_fetchItemDiffPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecItemDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::ITEM_DIFF,
                                                       KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const impl::ItemDiffRow diff = impl::unpackItemDiff(row);
        KiottySpecItemDiff& element = page->elements[page->length];
        element = KiottySpecItemDiff{};
        element.before = diff.before;
        element.after = diff.after;
        element.item = currentHandle(context->context, diff.id.view());
        element.kind = static_cast<KiottySpecItemKind>(diff.kind);
        element.change = static_cast<KiottySpecChange>(diff.change);
        element.changed_fields = diff.fields;
        copyText(element.id, diff.id);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openRelationDiffCursor(KiottySpecContext* context,
                                                         const KiottySpecDiffQuery* query)
{
    if (context == nullptr || query == nullptr) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const auto range = context->context.differ().range(query->from, query->to);
    if (!range) { return cursorFailure(range.code); }
    return cursorResult(context->context,
                        context->context.differ().relations(range.value, query->kind_mask));
}

KiottySpecStatusResult KiottySpec_fetchRelationDiffPage(KiottySpecContext* context,
                                                        KiottySpecCursor cursor,
                                                        KiottySpecRelationDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::RELATION_DIFF,
                                                       KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const impl::RelationDiffRow diff = impl::unpackRelationDiff(row);
        KiottySpecRelationDiff& element = page->elements[page->length];
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

KiottySpecItemResult KiottySpec_readItemAt(KiottySpecContext* context, KiottySpecText id,
                                           KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecItemResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const TextResult parsed = parseId(context->context, id);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(parsed.value, point.value);
    result.code = version.code;
    if (!version) { return result; }
    result.handle = currentHandle(context->context, parsed.value);
    result.value = toItem(version.value.record);
    return result;
}

KiottySpecTagResult KiottySpec_readItemTagAt(KiottySpecContext* context, KiottySpecText id,
                                             uint32_t tag, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecTagResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const TextResult parsed = parseId(context->context, id);
    if (!parsed) {
        result.code = parsed.code;
        return result;
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) {
        result.code = point.code;
        return result;
    }
    const auto version = differ.past().itemAt(parsed.value, point.value);
    if (!version) {
        result.code = version.code;
        return result;
    }
    if (tag >= context->context.catalog().model(version.value.record.kind).tags.length()) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                       KIOTTY_SPEC_SUBJECT_TAG, parsed.value);
        return result;
    }
    const auto value = differ.past().tagAt(parsed.value, tag, point.value);
    result.code = value.code;
    result.value = toTag(currentHandle(context->context, parsed.value),
                         static_cast<KiottySpecItemKind>(version.value.record.kind), tag,
                         value ? value.value : 0);
    return result;
}

KiottySpecSizeResult KiottySpec_readItemBodyAt(KiottySpecContext* context, KiottySpecText id,
                                               KiottySpecCheckpointHandle checkpoint,
                                               KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const TextResult parsed = parseId(context->context, id);
    if (!parsed) { return sizeResult(parsed.code, 0); }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return sizeResult(point.code, 0); }
    const auto read = differ.past().bodyAt(parsed.value, point.value, buffer.data, buffer.capacity);
    return sizeResult(read.code, read.value);
}

KiottySpecSizeResult KiottySpec_readDecisionAt(KiottySpecContext* context, KiottySpecItemKind kind,
                                               KiottySpecCheckpointHandle checkpoint,
                                               KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    if (!isKind(kind) || !entity::hasDecision(static_cast<entity::ItemKind>(kind))) {
        return sizeResult(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                           KIOTTY_SPEC_SUBJECT_KIND),
                          0);
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return sizeResult(point.code, 0); }
    const auto read = differ.past().decisionAt(static_cast<entity::ItemKind>(kind), point.value,
                                               buffer.data, buffer.capacity);
    return sizeResult(read.code, read.value);
}

KiottySpecCursorResult KiottySpec_openTextDiffCursor(KiottySpecContext* context,
                                                     KiottySpecItemHandle item,
                                                     KiottySpecTextField field,
                                                     const KiottySpecDiffQuery* range)
{
    if (context == nullptr || range == nullptr) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    if (!isTextField(field)) {
        return cursorFailure(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                              KIOTTY_SPEC_SUBJECT_TEXT_FIELD));
    }
    const auto record = context->context.workspace().items.reader().get(fromHandle(item));
    if (!record) {
        return cursorFailure(
            context->context.failure().raise(record.code, KIOTTY_SPEC_SUBJECT_ITEM));
    }
    impl::Differ& differ = context->context.differ();
    const auto points = differ.range(range->from, range->to);
    if (!points) { return cursorFailure(points.code); }
    return cursorResult(
        context->context,
        differ.text().open(fromHandle(item), static_cast<uint32_t>(field), points.value));
}

KiottySpecStatusResult KiottySpec_fetchTextDiffPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecTextDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    impl::TextDiffer differ = context->context.differ().text();
    while (page->length < KIOTTY_SPEC_PAGE_CAPACITY) {
        const auto piece = differ.next(cursor);
        if (!piece) { return status(piece.code); }
        if (piece.value.text.length() == 0) { break; }
        KiottySpecTextDiff& element = page->elements[page->length];
        element = KiottySpecTextDiff{};
        copyTerminated(impl::Span<char>(element.text, sizeof(element.text)), piece.value.text);
        element.length = static_cast<uint32_t>(piece.value.text.length());
        element.change = static_cast<KiottySpecTextChange>(piece.value.change);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}
