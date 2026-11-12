#include "implementation/context/kiotty_finder.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::RelationRecord;
using entity::TagRecord;

constexpr uint32_t GENERATION_SHIFT = 32;
constexpr uint32_t INDEX_MASK = 0xFFFFFFFFu;
constexpr uint32_t ORDINAL_SHIFT = 32;

struct ImpactSearch {
    Span<const RelationRecord> relations {};
    Span<uint8_t> visited {};
    Span<uint32_t> queue {};
};

static_assert(sizeof(ImpactSearch) == 48, "unexpected padding");

using ReachResult = Result<size_t, KiottySpecStatus>;

CursorRow makeRelationRow(const RelationRecord& relation);
KiottySpecStatus markImpact(const ImpactSearch& search, uint32_t start, uint32_t depth);
ReachResult visitLevel(const ImpactSearch& search, size_t head, size_t level_end);
ReachResult visitSources(const ImpactSearch& search, uint32_t target, size_t tail);
KiottySpecStatus visitItem(const ImpactSearch& search, size_t position, uint32_t index);
Result<bool, KiottySpecStatus> checkImpacted(Span<const uint8_t> visited, uint32_t start,
                                             uint32_t index);

}

CursorIdResult Finder::items(uint32_t kind, Span<const TagQuery> queries)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const bool is_selected = isSelected(handle, kind, queries);
        if (!is_selected) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

bool Finder::isSelected(ItemHandle handle, uint32_t kind, Span<const TagQuery> queries)
{
    const auto item = _workspace.items.reader().get(handle);
    if (!item) { return false; }
    const ItemKind item_kind = (*item.value).kind;
    const bool is_other_kind = kind != 0 && static_cast<uint32_t>(item_kind) != kind;
    if (is_other_kind) { return false; }
    for (const TagQuery& query : queries) {
        if (query.kind != item_kind) { return false; }
        const uint64_t tag_value = _workspace.items.tags().value(handle, query.tag);
        const bool has_tag = (tag_value & query.mask) != 0;
        if (!has_tag) { return false; }
    }
    return true;
}

CursorIdResult Finder::children(ItemHandle parent)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const auto item = _workspace.items.reader().get(handle);
        if (!item) { continue; }
        const ItemHandle item_parent = (*item.value).parent;
        const bool has_parent = item_parent.isValid();
        const bool is_child = has_parent && item_parent.index == parent.index;
        if (!is_child) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::impact(ItemHandle item, uint32_t depth)
{
    const auto cursor = _cursors.open(CursorKind::ID);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const ImpactSearch search {_workspace.relations.all(), viewVector(_visited),
                               viewVector(_queue)};
    const KiottySpecStatus marked = markImpact(search, item.index, depth);
    if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, marked); }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const Result<bool, KiottySpecStatus> impacted =
            checkImpacted(search.visited, item.index, handle.index);
        if (!impacted) { return writer.closeWith(cursor.value, impacted.code); }
        const bool is_impacted = impacted.value;
        if (!is_impacted) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::relations(ItemHandle item, KiottySpecDirection direction)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const bool is_upstream =
        direction == KIOTTY_SPEC_DIRECTION_UPSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool is_downstream =
        direction == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    for (const RelationRecord& relation : _workspace.relations.all()) {
        const bool is_linked = (is_upstream && relation.from.index == item.index) ||
                               (is_downstream && relation.to.index == item.index);
        if (!is_linked) { continue; }
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, makeRelationRow(relation));
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::relationsByModel(ItemKind kind, uint32_t relation_index)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle from = _workspace.items.reader().handleInIdOrder(position);
        const KiottySpecStatus pushed =
            pushRelationsFrom(cursor.value, from, kind, relation_index);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

KiottySpecStatus Finder::pushRelationsFrom(CursorId cursor, ItemHandle from, ItemKind kind,
                                           uint32_t relation_index)
{
    CursorWriter writer = makeCursorWriter();
    for (const RelationRecord& relation : _workspace.relations.all()) {
        const bool is_wanted = relation.from.index == from.index && relation.model == kind &&
                               relation.relation == relation_index;
        if (!is_wanted) { continue; }
        const KiottySpecStatus pushed = writer.pushRow(cursor, makeRelationRow(relation));
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

CursorIdResult Finder::tags(ItemHandle item)
{
    const auto cursor = _cursors.open(CursorKind::TAG);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    for (const TagRecord& tag : _workspace.items.tags().of(item)) {
        CursorRow row {};
        row.first = packHandle(tag.item);
        row.second = static_cast<uint64_t>(tag.model) |
                     (static_cast<uint64_t>(tag.tag) << ORDINAL_SHIFT);
        row.third = tag.value;
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

namespace {

CursorRow makeRelationRow(const RelationRecord& relation)
{
    CursorRow row {};
    row.first = packHandle(relation.from);
    row.second = packHandle(relation.to);
    row.third = static_cast<uint64_t>(relation.model) |
                (static_cast<uint64_t>(relation.relation) << ORDINAL_SHIFT);
    return row;
}

}

uint64_t packHandle(ItemHandle handle)
{
    return (static_cast<uint64_t>(handle.generation) << GENERATION_SHIFT) | handle.index;
}

ItemHandle unpackHandle(uint64_t value)
{
    return ItemHandle { static_cast<uint32_t>(value & INDEX_MASK),
                        static_cast<uint32_t>(value >> GENERATION_SHIFT) };
}

namespace {

KiottySpecStatus markImpact(const ImpactSearch& search, uint32_t start, uint32_t depth)
{
    for (uint8_t& flag : search.visited) { flag = 0; }
    const KiottySpecStatus seeded = visitItem(search, 0, start);
    if (seeded != KIOTTY_SPEC_STATUS_SUCCESS) { return seeded; }
    size_t head = 0;
    size_t tail = 1;
    for (uint32_t level = 0; level < depth && head < tail; ++level) {
        const ReachResult reached = visitLevel(search, head, tail);
        if (!reached) { return reached.code; }
        head = tail;
        tail = reached.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

ReachResult visitLevel(const ImpactSearch& search, size_t head, size_t level_end)
{
    size_t tail = level_end;
    for (size_t position = head; position < level_end; ++position) {
        const Result<uint32_t&, KiottySpecStatus> current = search.queue.at(position);
        if (!current) { return ReachResult::failure(current.code); }
        const ReachResult reached = visitSources(search, *current.value, tail);
        if (!reached) { return reached; }
        tail = reached.value;
    }
    return ReachResult::success(tail);
}

ReachResult visitSources(const ImpactSearch& search, uint32_t target, size_t tail)
{
    size_t next_tail = tail;
    for (const RelationRecord& relation : search.relations) {
        if (relation.to.index != target) { continue; }
        const Result<uint8_t&, KiottySpecStatus> flag = search.visited.at(relation.from.index);
        if (!flag) { return ReachResult::failure(flag.code); }
        const bool is_visited = *flag.value != 0;
        if (is_visited) { continue; }
        const KiottySpecStatus status = visitItem(search, next_tail, relation.from.index);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return ReachResult::failure(status); }
        ++next_tail;
    }
    return ReachResult::success(next_tail);
}

KiottySpecStatus visitItem(const ImpactSearch& search, size_t position, uint32_t index)
{
    const Result<uint8_t&, KiottySpecStatus> flag = search.visited.at(index);
    if (!flag) { return flag.code; }
    const Result<uint32_t&, KiottySpecStatus> slot = search.queue.at(position);
    if (!slot) { return slot.code; }
    *flag.value = 1;
    *slot.value = index;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> checkImpacted(Span<const uint8_t> visited, uint32_t start,
                                             uint32_t index)
{
    const Result<const uint8_t&, KiottySpecStatus> flag = visited.at(index);
    if (!flag) { return Result<bool, KiottySpecStatus>::failure(flag.code); }
    const bool is_visited = *flag.value != 0;
    return Result<bool, KiottySpecStatus>::success(index != start && is_visited);
}

}

}
