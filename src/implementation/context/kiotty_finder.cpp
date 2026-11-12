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
constexpr uint64_t MAX_GENERATION = 0xFFFFu;

constexpr uint8_t UNVISITED = 0;
constexpr uint8_t VISITED = 1;
constexpr uint8_t FRONTIER = 2;
constexpr uint8_t NEXT = 3;
constexpr size_t BISECTION_DIVISOR = 2;

using ReachResult = Result<bool, KiottySpecStatus>;
using ItemStateResult = Result<const entity::ItemState&, KiottySpecStatus>;

CursorRow makeRelationRow(const RelationRecord& relation);
CursorRow makeTagRow(const TagRecord& tag);
KiottySpecStatus markImpact(Span<const RelationRecord> relations, Span<uint8_t> marks,
                            uint32_t start, uint32_t depth);
ReachResult visitLevel(Span<const RelationRecord> relations, Span<uint8_t> marks);
uint8_t advanceMark(uint8_t mark);
bool isMarkedAt(Span<const uint8_t> marks, size_t index);
Span<const RelationRecord> selectOutgoing(Span<const RelationRecord> relations, uint32_t index);
size_t findFirstFrom(Span<const RelationRecord> relations, uint32_t index);

}

CursorIdResult Finder::items(uint32_t kind, Span<const TagQuery> queries)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    markKind(kind);
    for (const TagQuery& query : queries) { keepTagged(query); }
    const KiottySpecStatus pushed = pushMarked(cursor.value);
    if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    return cursor;
}

void Finder::markKind(uint32_t kind)
{
    const Span<uint8_t> marks = viewVector(_marks);
    const ItemSlots slots = _workspace.items.slots();
    const size_t length = marks.length();
    for (size_t index = 0; index < length; ++index) {
        const ItemStateResult item = _workspace.items.reader().get(slots.handleAt(index));
        const uint32_t item_kind = item ? static_cast<uint32_t>((*item.value).kind) : 0;
        const bool is_selected = item && (kind == 0 || item_kind == kind);
        marks[index] = is_selected ? 1 : 0;
    }
}

void Finder::keepTagged(TagQuery query)
{
    const Span<uint64_t> values = viewVector(_tag_values);
    for (uint64_t& value : values) { value = 0; }
    _workspace.items.tags().copyValues(query.kind, query.tag, values);
    const Span<uint8_t> marks = viewVector(_marks);
    const size_t length = marks.length();
    for (size_t index = 0; index < length; ++index) {
        const Result<uint64_t&, KiottySpecStatus> value = values.at(index);
        const bool has_tag = value && (*value.value & query.mask) != 0;
        if (!has_tag) { marks[index] = 0; }
    }
}

CursorIdResult Finder::children(ItemHandle parent)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    markChildren(parent);
    const KiottySpecStatus pushed = pushMarked(cursor.value);
    if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    return cursor;
}

void Finder::markChildren(ItemHandle parent)
{
    const Span<uint8_t> marks = viewVector(_marks);
    const ItemSlots slots = _workspace.items.slots();
    const size_t length = marks.length();
    for (size_t index = 0; index < length; ++index) {
        const ItemStateResult item = _workspace.items.reader().get(slots.handleAt(index));
        const ItemHandle item_parent = item ? (*item.value).parent : ItemHandle {};
        const bool has_parent = item_parent.isValid();
        const bool is_child = has_parent && item_parent.index == parent.index;
        marks[index] = is_child ? 1 : 0;
    }
}

CursorIdResult Finder::impact(ItemHandle item, uint32_t depth)
{
    const auto cursor = _cursors.open(CursorKind::ID);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const Span<uint8_t> marks = viewVector(_marks);
    const KiottySpecStatus marked =
        markImpact(_workspace.relations.all(), marks, item.index, depth);
    if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, marked); }
    const Result<uint8_t&, KiottySpecStatus> start = marks.at(item.index);
    if (start) { *start.value = UNVISITED; }
    const KiottySpecStatus pushed = pushMarked(cursor.value);
    if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    return cursor;
}

KiottySpecStatus Finder::pushMarked(CursorId cursor)
{
    CursorWriter writer = makeCursorWriter();
    const ItemSlots slots = _workspace.items.slots();
    const Span<const uint8_t> marks = viewVector(_marks);
    const size_t item_count = slots.count();
    for (size_t position = 0; position < item_count; ++position) {
        const uint32_t index = slots.readIdOrder(position);
        const bool is_marked = isMarkedAt(marks, index);
        if (!is_marked) { continue; }
        CursorRow row {};
        row.first = packHandle(slots.handleAt(index));
        const KiottySpecStatus pushed = writer.pushRow(cursor, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
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
    markSources(kind, relation_index);
    const ItemSlots slots = _workspace.items.slots();
    const Span<const uint8_t> marks = viewVector(_marks);
    const size_t item_count = slots.count();
    for (size_t position = 0; position < item_count; ++position) {
        const uint32_t index = slots.readIdOrder(position);
        const bool is_marked = isMarkedAt(marks, index);
        if (!is_marked) { continue; }
        const KiottySpecStatus pushed =
            pushRelationsFrom(cursor.value, index, kind, relation_index);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

void Finder::markSources(ItemKind kind, uint32_t relation_index)
{
    const Span<uint8_t> marks = viewVector(_marks);
    for (uint8_t& mark : marks) { mark = UNVISITED; }
    for (const RelationRecord& relation : _workspace.relations.all()) {
        const bool is_wanted = relation.model == kind && relation.relation == relation_index;
        if (!is_wanted) { continue; }
        const Result<uint8_t&, KiottySpecStatus> mark = marks.at(relation.from.index);
        if (mark) { *mark.value = VISITED; }
    }
}

KiottySpecStatus Finder::pushRelationsFrom(CursorId cursor, uint32_t from_index, ItemKind kind,
                                           uint32_t relation_index)
{
    CursorWriter writer = makeCursorWriter();
    const Span<const RelationRecord> outgoing =
        selectOutgoing(_workspace.relations.all(), from_index);
    for (const RelationRecord& relation : outgoing) {
        const bool is_wanted = relation.model == kind && relation.relation == relation_index;
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
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, makeTagRow(tag));
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

CursorRow makeTagRow(const TagRecord& tag)
{
    CursorRow row {};
    row.first = packHandle(tag.item);
    row.second =
        static_cast<uint64_t>(tag.model) | (static_cast<uint64_t>(tag.tag) << ORDINAL_SHIFT);
    row.third = tag.value;
    return row;
}

}

uint64_t packHandle(ItemHandle handle)
{
    return (static_cast<uint64_t>(handle.generation) << GENERATION_SHIFT) | handle.index;
}

ItemHandle unpackHandle(uint64_t value)
{
    const uint64_t generation = value >> GENERATION_SHIFT;
    const bool is_out_of_range = generation > MAX_GENERATION;
    if (is_out_of_range) { return ItemHandle {}; }
    return ItemHandle {static_cast<uint32_t>(value & INDEX_MASK),
                       static_cast<uint16_t>(generation)};
}

namespace {

KiottySpecStatus markImpact(Span<const RelationRecord> relations, Span<uint8_t> marks,
                            uint32_t start, uint32_t depth)
{
    for (uint8_t& mark : marks) { mark = UNVISITED; }
    const Result<uint8_t&, KiottySpecStatus> seed = marks.at(start);
    if (!seed) { return seed.code; }
    *seed.value = FRONTIER;
    bool has_frontier = true;
    for (uint32_t level = 0; level < depth && has_frontier; ++level) {
        const ReachResult reached = visitLevel(relations, marks);
        if (!reached) { return reached.code; }
        has_frontier = reached.value;
        for (uint8_t& mark : marks) { mark = advanceMark(mark); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

ReachResult visitLevel(Span<const RelationRecord> relations, Span<uint8_t> marks)
{
    bool has_reached = false;
    for (const RelationRecord& relation : relations) {
        const Result<uint8_t&, KiottySpecStatus> target = marks.at(relation.to.index);
        const bool is_frontier_target = target && *target.value == FRONTIER;
        if (!is_frontier_target) { continue; }
        const Result<uint8_t&, KiottySpecStatus> source = marks.at(relation.from.index);
        if (!source) { return ReachResult::failure(source.code); }
        const bool is_unvisited = *source.value == UNVISITED;
        if (!is_unvisited) { continue; }
        *source.value = NEXT;
        has_reached = true;
    }
    return ReachResult::success(has_reached);
}

uint8_t advanceMark(uint8_t mark)
{
    if (mark == FRONTIER) { return VISITED; }
    if (mark == NEXT) { return FRONTIER; }
    return mark;
}

bool isMarkedAt(Span<const uint8_t> marks, size_t index)
{
    const Result<const uint8_t&, KiottySpecStatus> mark = marks.at(index);
    return mark && *mark.value != UNVISITED;
}

Span<const RelationRecord> selectOutgoing(Span<const RelationRecord> relations, uint32_t index)
{
    const size_t first = findFirstFrom(relations, index);
    const size_t length = relations.length();
    size_t last = first;
    while (last < length && relations[last].from.index == index) { ++last; }
    return relations.subspan(first, last - first).value;
}

size_t findFirstFrom(Span<const RelationRecord> relations, uint32_t index)
{
    size_t low = 0;
    size_t high = relations.length();
    while (low < high) {
        const size_t middle = low + (high - low) / BISECTION_DIVISOR;
        const Result<const RelationRecord&, KiottySpecStatus> probe = relations.at(middle);
        const bool is_before = probe && (*probe.value).from.index < index;
        if (is_before) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

}

}
