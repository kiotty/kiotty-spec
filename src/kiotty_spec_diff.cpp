
#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

namespace {

KiottySpecKindDiff convertKindDiff(const impl::KindCounts& count);
KiottySpecItemHandle findCurrentHandle(impl::ItemReader reader, impl::Span<const char> id);
bool isTextField(KiottySpecTextField field);

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
        result.kinds[index] = convertKindDiff(counts[index]);
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
    const impl::CursorIdResult opened =
        context->context.differ().items(range.value, query->kind_mask, query->field_mask);
    return Boundary(context->context).wrap(opened);
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
    const impl::Span<KiottySpecItemDiff> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    const impl::ItemReader reader = context->context.workspace().items.reader();
    for (const impl::CursorRow& row : rows.value) {
        const impl::ItemDiffRow diff = impl::unpackItemDiff(row);
        const auto slot = elements.at(page->length);
        if (!slot) { return status(slot.code); }
        KiottySpecItemDiff& element = *slot.value;
        element = KiottySpecItemDiff{};
        element.before = diff.before;
        element.after = diff.after;
        element.item = findCurrentHandle(reader, diff.id.view());
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
    const impl::CursorIdResult opened =
        context->context.differ().relations(range.value, query->kind_mask);
    return Boundary(context->context).wrap(opened);
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
    const impl::Span<KiottySpecRelationDiff> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    for (const impl::CursorRow& row : rows.value) {
        const impl::RelationDiffRow diff = impl::unpackRelationDiff(row);
        const auto slot = elements.at(page->length);
        if (!slot) { return status(slot.code); }
        KiottySpecRelationDiff& element = *slot.value;
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
    const TextResult parsed = Boundary(context->context).parse(id, Field::ID);
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
    result.handle = findCurrentHandle(context->context.workspace().items.reader(), parsed.value);
    result.value = toItem(version.value.record);
    return result;
}

KiottySpecTagResult KiottySpec_readItemTagAt(KiottySpecContext* context, KiottySpecText id,
                                             uint32_t tag, KiottySpecCheckpointHandle checkpoint)
{
    KiottySpecTagResult result {};
    result.code = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    if (context == nullptr) { return result; }
    const TextResult parsed = Boundary(context->context).parse(id, Field::ID);
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
    const entity::ItemKind kind = version.value.record.kind;
    const size_t tag_count = context->context.catalog().model(kind).tags.length();
    if (tag >= tag_count) {
        result.code = context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                       KIOTTY_SPEC_SUBJECT_TAG, parsed.value);
        return result;
    }
    const auto value = differ.past().tagAt(parsed.value, tag, point.value);
    result.code = value.code;
    const impl::ItemReader reader = context->context.workspace().items.reader();
    result.value = toTag(findCurrentHandle(reader, parsed.value),
                         static_cast<KiottySpecItemKind>(kind), tag, value ? value.value : 0);
    return result;
}

KiottySpecSizeResult KiottySpec_readItemBodyAt(KiottySpecContext* context, KiottySpecText id,
                                               KiottySpecCheckpointHandle checkpoint,
                                               KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const TextResult parsed = Boundary(context->context).parse(id, Field::ID);
    if (!parsed) { return sizeResult(parsed.code, 0); }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return sizeResult(point.code, 0); }
    const impl::Span<char> target = toSpan(buffer);
    const auto read = differ.past().readBodyAt(parsed.value, point.value, target);
    return sizeResult(read.code, read.value);
}

KiottySpecSizeResult KiottySpec_readDecisionAt(KiottySpecContext* context, KiottySpecItemKind kind,
                                               KiottySpecCheckpointHandle checkpoint,
                                               KiottySpecBuffer buffer)
{
    if (context == nullptr) { return sizeResult(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, 0); }
    const bool is_valid_kind = isKind(kind);
    const bool has_decision =
        is_valid_kind && entity::hasDecision(static_cast<entity::ItemKind>(kind));
    if (!has_decision) {
        return sizeResult(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                           KIOTTY_SPEC_SUBJECT_KIND),
                          0);
    }
    impl::Differ& differ = context->context.differ();
    const auto point = differ.point(checkpoint);
    if (!point) { return sizeResult(point.code, 0); }
    const impl::Span<char> target = toSpan(buffer);
    const auto read =
        differ.past().readDecisionAt(static_cast<entity::ItemKind>(kind), point.value, target);
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
    const bool is_text_field = isTextField(field);
    if (!is_text_field) {
        return cursorFailure(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                              KIOTTY_SPEC_SUBJECT_TEXT_FIELD));
    }
    const Boundary boundary(context->context);
    const RecordResult record = boundary.read(item);
    if (!record) { return cursorFailure(record.code); }
    impl::Differ& differ = context->context.differ();
    const auto points = differ.range(range->from, range->to);
    if (!points) { return cursorFailure(points.code); }
    const impl::CursorIdResult opened =
        differ.text().open(fromHandle(item), static_cast<uint32_t>(field), points.value);
    return boundary.wrap(opened);
}

KiottySpecStatusResult KiottySpec_fetchTextDiffPage(KiottySpecContext* context,
                                                    KiottySpecCursor cursor,
                                                    KiottySpecTextDiffPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    impl::TextDiffer differ = context->context.differ().text();
    const impl::Span<KiottySpecTextDiff> elements(page->elements, KIOTTY_SPEC_PAGE_CAPACITY);
    while (page->length < KIOTTY_SPEC_PAGE_CAPACITY) {
        const auto piece = differ.next(cursor);
        if (!piece) { return status(piece.code); }
        const size_t piece_length = piece.value.text.length();
        if (piece_length == 0) { break; }
        const auto slot = elements.at(page->length);
        if (!slot) { return status(slot.code); }
        KiottySpecTextDiff& element = *slot.value;
        element = KiottySpecTextDiff{};
        copyTerminated(impl::Span<char>(element.text, sizeof(element.text)), piece.value.text);
        element.length = static_cast<uint32_t>(piece.value.text.length());
        element.change = static_cast<KiottySpecTextChange>(piece.value.change);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

}

namespace {

KiottySpecKindDiff convertKindDiff(const impl::KindCounts& count)
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

KiottySpecItemHandle findCurrentHandle(impl::ItemReader reader, impl::Span<const char> id)
{
    const auto handle = reader.find(id);
    return handle ? toHandle(handle.value) : KIOTTY_SPEC_INVALID_ITEM_HANDLE;
}

bool isTextField(KiottySpecTextField field)
{ return field == KIOTTY_SPEC_TEXT_FIELD_TITLE || field == KIOTTY_SPEC_TEXT_FIELD_SUMMARY; }

}
