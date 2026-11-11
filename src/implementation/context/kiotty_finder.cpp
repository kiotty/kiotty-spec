#include "implementation/context/kiotty_finder.h"

#include "implementation/context/kiotty_cursor_rows.h"
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

KiottySpecStatus markImpact(Span<uint8_t> visited, Span<uint32_t> queue,
                            Span<const RelationRecord> relations, uint32_t start, uint32_t depth);
Result<size_t, KiottySpecStatus> visitLevel(Span<uint8_t> visited, Span<uint32_t> queue,
                                            Span<const RelationRecord> relations, size_t head,
                                            size_t level_end);
Result<size_t, KiottySpecStatus> visitSources(Span<uint8_t> visited, Span<uint32_t> queue,
                                              Span<const RelationRecord> relations,
                                              uint32_t target, size_t tail);
KiottySpecStatus visitItem(Span<uint8_t> visited, Span<uint32_t> queue, size_t position,
                           uint32_t index);
Result<bool, KiottySpecStatus> checkImpacted(Span<const uint8_t> visited, uint32_t start,
                                             uint32_t index);

}

CursorIdResult Finder::items(uint32_t kind, Span<const TagQuery> queries)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const auto item = _workspace.items.reader().get(handle);
        if (!item) { continue; }
        if (kind != 0 && static_cast<uint32_t>(item.value->kind) != kind) { continue; }
        bool is_selected = true;
        for (const TagQuery& query : queries) {
            is_selected = query.kind == item.value->kind;
            if (!is_selected) { break; }
            const uint64_t tag_value = _workspace.items.tags().value(handle, query.tag);
            is_selected = (tag_value & query.mask) != 0;
            if (!is_selected) { break; }
        }
        if (!is_selected) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _workspace.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::children(ItemHandle parent)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const auto item = _workspace.items.reader().get(handle);
        if (!item) { continue; }
        const ItemHandle item_parent = item.value->parent;
        const bool has_parent = item_parent.isValid();
        const bool is_child = has_parent && item_parent.index == parent.index;
        if (!is_child) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _workspace.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::impact(ItemHandle item, uint32_t depth)
{
    const auto cursor = _cursors.open(CursorKind::ID);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const Span<uint8_t> visited = viewVector(_visited);
    const KiottySpecStatus marked = markImpact(visited, viewVector(_queue),
                                               _workspace.relations.all(), item.index, depth);
    if (marked != KIOTTY_SPEC_STATUS_SUCCESS) { return closeWith(_cursors, cursor.value, marked); }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _workspace.items.reader().handleInIdOrder(position);
        const Result<bool, KiottySpecStatus> impacted =
            checkImpacted(visited, item.index, handle.index);
        if (!impacted) { return closeWith(_cursors, cursor.value, impacted.code); }
        const bool is_impacted = impacted.value;
        if (!is_impacted) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _workspace.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::relations(ItemHandle item, KiottySpecDirection direction)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const bool is_upstream =
        direction == KIOTTY_SPEC_DIRECTION_UPSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool is_downstream =
        direction == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    for (const RelationRecord& relation : _workspace.relations.all()) {
        const bool is_linked = (is_upstream && relation.from.index == item.index) ||
                               (is_downstream && relation.to.index == item.index);
        if (!is_linked) { continue; }
        CursorRow row {};
        row.first = packHandle(relation.from);
        row.second = packHandle(relation.to);
        row.third = static_cast<uint64_t>(relation.model) |
                    (static_cast<uint64_t>(relation.relation) << ORDINAL_SHIFT);
        const KiottySpecStatus pushed = pushRow(_cursors, _workspace.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::relationsByModel(ItemKind kind, uint32_t relation_index)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const size_t item_count = _workspace.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle from = _workspace.items.reader().handleInIdOrder(position);
        for (const RelationRecord& relation : _workspace.relations.all()) {
            if (relation.from.index != from.index || relation.model != kind ||
                relation.relation != relation_index) {
                continue;
            }
            CursorRow row {};
            row.first = packHandle(relation.from);
            row.second = packHandle(relation.to);
            row.third = static_cast<uint64_t>(relation.model) |
                        (static_cast<uint64_t>(relation.relation) << ORDINAL_SHIFT);
            const KiottySpecStatus pushed =
                pushRow(_cursors, _workspace.failure, cursor.value, row);
            if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
                return closeWith(_cursors, cursor.value, pushed);
            }
        }
    }
    return cursor;
}

CursorIdResult Finder::tags(ItemHandle item)
{
    const auto cursor = _cursors.open(CursorKind::TAG);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    for (const TagRecord& tag : _workspace.items.tags().of(item)) {
        CursorRow row {};
        row.first = packHandle(tag.item);
        row.second = static_cast<uint64_t>(tag.model) |
                     (static_cast<uint64_t>(tag.tag) << ORDINAL_SHIFT);
        row.third = tag.value;
        const KiottySpecStatus pushed = pushRow(_cursors, _workspace.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
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

KiottySpecStatus markImpact(Span<uint8_t> visited, Span<uint32_t> queue,
                            Span<const RelationRecord> relations, uint32_t start, uint32_t depth)
{
    for (uint8_t& flag : visited) { flag = 0; }
    const KiottySpecStatus seeded = visitItem(visited, queue, 0, start);
    if (seeded != KIOTTY_SPEC_STATUS_SUCCESS) { return seeded; }
    size_t head = 0;
    size_t tail = 1;
    for (uint32_t level = 0; level < depth && head < tail; ++level) {
        const Result<size_t, KiottySpecStatus> reached =
            visitLevel(visited, queue, relations, head, tail);
        if (!reached) { return reached.code; }
        head = tail;
        tail = reached.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> visitLevel(Span<uint8_t> visited, Span<uint32_t> queue,
                                            Span<const RelationRecord> relations, size_t head,
                                            size_t level_end)
{
    size_t tail = level_end;
    for (size_t position = head; position < level_end; ++position) {
        const Result<uint32_t&, KiottySpecStatus> current = queue.at(position);
        if (!current) { return Result<size_t, KiottySpecStatus>::failure(current.code); }
        const Result<size_t, KiottySpecStatus> reached =
            visitSources(visited, queue, relations, *current.value, tail);
        if (!reached) { return reached; }
        tail = reached.value;
    }
    return Result<size_t, KiottySpecStatus>::success(tail);
}

Result<size_t, KiottySpecStatus> visitSources(Span<uint8_t> visited, Span<uint32_t> queue,
                                              Span<const RelationRecord> relations,
                                              uint32_t target, size_t tail)
{
    size_t next_tail = tail;
    for (const RelationRecord& relation : relations) {
        if (relation.to.index != target) { continue; }
        const Result<uint8_t&, KiottySpecStatus> flag = visited.at(relation.from.index);
        if (!flag) { return Result<size_t, KiottySpecStatus>::failure(flag.code); }
        const bool is_visited = *flag.value != 0;
        if (is_visited) { continue; }
        const KiottySpecStatus status = visitItem(visited, queue, next_tail, relation.from.index);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
            return Result<size_t, KiottySpecStatus>::failure(status);
        }
        ++next_tail;
    }
    return Result<size_t, KiottySpecStatus>::success(next_tail);
}

KiottySpecStatus visitItem(Span<uint8_t> visited, Span<uint32_t> queue, size_t position,
                           uint32_t index)
{
    const Result<uint8_t&, KiottySpecStatus> flag = visited.at(index);
    if (!flag) { return flag.code; }
    const Result<uint32_t&, KiottySpecStatus> slot = queue.at(position);
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
