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

}

CursorIdResult Finder::items(uint32_t kind, Span<const TagQuery> queries)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    const size_t item_count = _w.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _w.items.reader().handleInIdOrder(position);
        const auto item = _w.items.reader().get(handle);
        if (!item) { continue; }
        if (kind != 0 && static_cast<uint32_t>(item.value->kind) != kind) { continue; }
        bool selected = true;
        for (const TagQuery& query : queries) {
            selected = query.kind == item.value->kind;
            if (!selected) { break; }
            const uint64_t tag_value = _w.items.tags().value(handle, query.tag);
            selected = (tag_value & query.mask) != 0;
            if (!selected) { break; }
        }
        if (!selected) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::children(ItemHandle parent)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    const size_t item_count = _w.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _w.items.reader().handleInIdOrder(position);
        const auto item = _w.items.reader().get(handle);
        if (!item) { continue; }
        const ItemHandle item_parent = item.value->parent;
        const bool has_parent = item_parent.isValid();
        if (!has_parent || item_parent.index != parent.index) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::impact(ItemHandle item, uint32_t depth)
{
    const auto cursor = _cursors.open(CursorKind::ID);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    for (auto& flag : _visited) { flag = 0; }
    size_t head = 0;
    size_t tail = 0;
    _queue[tail++] = item.index;
    _visited[item.index] = 1;
    for (uint32_t level = 0; level < depth && head < tail; ++level) {
        const size_t level_end = tail;
        while (head < level_end) {
            const uint32_t current = _queue[head++];
            for (const RelationRecord& relation : _w.relations.all()) {
                if (relation.to.index != current || _visited[relation.from.index] != 0) {
                    continue;
                }
                _visited[relation.from.index] = 1;
                _queue[tail++] = relation.from.index;
            }
        }
    }
    const size_t item_count = _w.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle handle = _w.items.reader().handleInIdOrder(position);
        if (handle.index == item.index || _visited[handle.index] == 0) { continue; }
        CursorRow row {};
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::relations(ItemHandle item, KiottySpecDirection direction)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    const bool up =
        direction == KIOTTY_SPEC_DIRECTION_UPSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool down =
        direction == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    for (const RelationRecord& relation : _w.relations.all()) {
        if (!((up && relation.from.index == item.index) ||
              (down && relation.to.index == item.index))) {
            continue;
        }
        CursorRow row {};
        row.first = packHandle(relation.from);
        row.second = packHandle(relation.to);
        row.third = static_cast<uint64_t>(relation.model) |
                    (static_cast<uint64_t>(relation.relation) << ORDINAL_SHIFT);
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) {
            return closeWith(_cursors, cursor.value, pushed);
        }
    }
    return cursor;
}

CursorIdResult Finder::relationsByModel(ItemKind kind, uint32_t relation_index)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    const size_t item_count = _w.items.reader().count();
    for (size_t position = 0; position < item_count; ++position) {
        const ItemHandle from = _w.items.reader().handleInIdOrder(position);
        for (const RelationRecord& relation : _w.relations.all()) {
            if (relation.from.index != from.index || relation.model != kind ||
                relation.relation != relation_index) {
                continue;
            }
            CursorRow row {};
            row.first = packHandle(relation.from);
            row.second = packHandle(relation.to);
            row.third = static_cast<uint64_t>(relation.model) |
                        (static_cast<uint64_t>(relation.relation) << ORDINAL_SHIFT);
            const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
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
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    for (const TagRecord& tag : _w.items.tags().of(item)) {
        CursorRow row {};
        row.first = packHandle(tag.item);
        row.second = static_cast<uint64_t>(tag.model) |
                     (static_cast<uint64_t>(tag.tag) << ORDINAL_SHIFT);
        row.third = tag.value;
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
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

}
