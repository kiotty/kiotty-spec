// C 인터페이스: 관계 · 목록 커서. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾸는 일만 한다.

#include "kiotty_spec_adapter.h"

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;
using namespace kiotty::spec::adapter;

extern "C" {

KiottySpecStatusResult KiottySpec_addRelation(KiottySpecContext* context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult from = parseId(context->context, from_id);
    if (!from) { return status(from.code); }
    const TextResult to = parseId(context->context, to_id);
    if (!to) { return status(to.code); }
    return status(context->context.relationEditor().add(from.value, relation, to.value));
}

KiottySpecStatusResult KiottySpec_removeRelation(KiottySpecContext* context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const TextResult from = parseId(context->context, from_id);
    if (!from) { return status(from.code); }
    const TextResult to = parseId(context->context, to_id);
    if (!to) { return status(to.code); }
    return status(context->context.relationEditor().remove(from.value, relation, to.value));
}

KiottySpecCursorResult KiottySpec_openRelationCursor(KiottySpecContext* context, KiottySpecText id, KiottySpecDirection direction)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (direction < KIOTTY_SPEC_DIRECTION_UPSTREAM || direction > KIOTTY_SPEC_DIRECTION_BOTH) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    const auto handle = resolve(context->context, id);
    if (!handle) { return cursorFailure(handle.code); }
    return cursorResult(context->context, context->context.finder().relations(handle.value, direction));
}

KiottySpecCursorResult KiottySpec_openRelationCursorByModel(KiottySpecContext* context, KiottySpecItemKind kind, uint32_t relation)
{
    if (context == nullptr || !isKind(kind)) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (relation >= context->context.catalog().model(static_cast<entity::ItemKind>(kind)).relations.length()) {
        return cursorFailure(context->context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_RELATION));
    }
    return cursorResult(context->context, context->context.finder().relationsByModel(static_cast<entity::ItemKind>(kind), relation));
}

KiottySpecStatusResult KiottySpec_fetchRelationPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecRelationPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::RELATION, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        KiottySpecRelation& element = page->elements[index];
        element = KiottySpecRelation{};
        element.from = row.first;
        element.to = row.second;
        element.model = static_cast<KiottySpecItemKind>(row.third & LOW_MASK);
        element.relation = static_cast<uint32_t>(row.third >> SHIFT);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecCursorResult KiottySpec_openItemCursor(KiottySpecContext* context, KiottySpecItemKind kind, const KiottySpecTagQuery* tag_queries, size_t count)
{
    if (context == nullptr || (kind != 0 && !isKind(kind)) || count > KIOTTY_SPEC_MAX_TAG_QUERIES || (count > 0 && tag_queries == nullptr)) {
        return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    impl::TagQuery queries[KIOTTY_SPEC_MAX_TAG_QUERIES] {};
    for (size_t index = 0; index < count; ++index) {
        if (!isKind(tag_queries[index].kind)) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        queries[index] = impl::TagQuery{static_cast<entity::ItemKind>(tag_queries[index].kind), tag_queries[index].tag_index, tag_queries[index].mask};
    }
    return cursorResult(context->context, context->context.finder().items(kind, impl::Span<const impl::TagQuery>(queries, count)));
}

KiottySpecCursorResult KiottySpec_openChildCursor(KiottySpecContext* context, KiottySpecText id)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const auto handle = resolve(context->context, id);
    if (!handle) { return cursorFailure(handle.code); }
    return cursorResult(context->context, context->context.finder().children(handle.value));
}

KiottySpecCursorResult KiottySpec_openImpactCursor(KiottySpecContext* context, KiottySpecText id, uint32_t depth)
{
    if (context == nullptr) { return cursorFailure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const auto handle = resolve(context->context, id);
    if (!handle) { return cursorFailure(handle.code); }
    return cursorResult(context->context, context->context.finder().impact(handle.value, depth));
}

KiottySpecStatusResult KiottySpec_fetchItemPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecItemPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::ITEM, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        const auto record = items.reader().get(fromHandle(row.first));
        if (!record) { continue; }
        page->elements[index] = toItem(*record.value);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecStatusResult KiottySpec_fetchIdPage(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecIdPage* page)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    impl::ItemStore& items = context->context.workspace().items;
    if (page == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    page->length = 0;
    const auto rows = context->context.cursors().fetch(cursor, impl::CursorKind::ID, KIOTTY_SPEC_PAGE_CAPACITY);
    if (!rows) { return status(rows.code); }
    for (const impl::CursorRow& row : rows.value) {
        const size_t index = page->length;
        const auto record = items.reader().get(fromHandle(row.first));
        if (!record) { continue; }
        copyText(page->elements[index], record.value->id);
        ++page->length;
    }
    return status(KIOTTY_SPEC_STATUS_SUCCESS);
}

KiottySpecStatusResult KiottySpec_closeCursor(KiottySpecContext* context, KiottySpecCursor cursor)
{
    if (context == nullptr) { return status(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return status(context->context.cursors().close(cursor));
}

}
