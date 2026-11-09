#include "implementation/context/finder.h"

#include "implementation/core/text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;
using entity::ItemHandle;
using entity::ItemKind;
using entity::RelationRecord;
using entity::TagRecord;

constexpr uint32_t SHIFT = 32;

}

uint64_t packHandle(ItemHandle handle)
{
    return (static_cast<uint64_t>(handle.generation) << SHIFT) | handle.index;
}

ItemHandle unpackHandle(uint64_t value)
{
    return ItemHandle{static_cast<uint32_t>(value & 0xFFFFFFFFu), static_cast<uint32_t>(value >> SHIFT)};
}

KiottySpecStatus Finder::push(CursorId cursor, const CursorRow& row)
{
    const KiottySpecStatus pushed = _cursors.push(cursor, row);
    if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        return _w.failure.raise(pushed, {literalText("결과 행이 max_cursor_rows 를 넘음. 조건을 좁힐 것")});
    }
    return pushed;
}

CursorIdResult Finder::fail(CursorId cursor, KiottySpecStatus status)
{
    _cursors.close(cursor);
    return CursorIdResult::failure(status);
}

CursorIdResult Finder::items(uint32_t kind, Span<const TagQuery> queries)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    for (size_t position = 0; position < _w.items.count(); ++position) {
        const ItemHandle handle = _w.items.handleInIdOrder(position);
        const auto item = _w.items.get(handle);
        if (!item) { continue; }
        if (kind != 0 && static_cast<uint32_t>(item.value->kind) != kind) { continue; }
        bool selected = true;
        for (const TagQuery& query : queries) {
            if (query.kind != item.value->kind || (_w.items.tagValue(handle, query.tag) & query.mask) == 0) {
                selected = false;
                break;
            }
        }
        if (!selected) { continue; }
        CursorRow row;
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::children(ItemHandle parent)
{
    const auto cursor = _cursors.open(CursorKind::ITEM);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    for (size_t position = 0; position < _w.items.count(); ++position) {
        const ItemHandle handle = _w.items.handleInIdOrder(position);
        const auto item = _w.items.get(handle);
        if (!item || !item.value->parent.isValid() || item.value->parent.index != parent.index) { continue; }
        CursorRow row;
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::impact(ItemHandle item, uint32_t depth)
{
    const auto cursor = _cursors.open(CursorKind::ID);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
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
                if (relation.to.index != current || _visited[relation.from.index] != 0) { continue; }
                _visited[relation.from.index] = 1;
                _queue[tail++] = relation.from.index;
            }
        }
    }
    // 시작 항목을 빼고 ID 순서로 담는다.
    for (size_t position = 0; position < _w.items.count(); ++position) {
        const ItemHandle handle = _w.items.handleInIdOrder(position);
        if (handle.index == item.index || _visited[handle.index] == 0) { continue; }
        CursorRow row;
        row.first = packHandle(handle);
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::relations(ItemHandle item, KiottySpecDirection direction)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    const bool up = direction == KIOTTY_SPEC_DIRECTION_UPSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    const bool down = direction == KIOTTY_SPEC_DIRECTION_DOWNSTREAM || direction == KIOTTY_SPEC_DIRECTION_BOTH;
    for (const RelationRecord& relation : _w.relations.all()) {
        if (!((up && relation.from.index == item.index) || (down && relation.to.index == item.index))) { continue; }
        CursorRow row;
        row.first = packHandle(relation.from);
        row.second = packHandle(relation.to);
        row.third = static_cast<uint64_t>(relation.model) | (static_cast<uint64_t>(relation.relation) << SHIFT);
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::relationsByModel(ItemKind kind, uint32_t relation_index)
{
    const auto cursor = _cursors.open(CursorKind::RELATION);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    // from 의 ID 순서로 담는다 (8.4).
    for (size_t position = 0; position < _w.items.count(); ++position) {
        const ItemHandle from = _w.items.handleInIdOrder(position);
        for (const RelationRecord& relation : _w.relations.all()) {
            if (relation.from.index != from.index || relation.model != kind || relation.relation != relation_index) { continue; }
            CursorRow row;
            row.first = packHandle(relation.from);
            row.second = packHandle(relation.to);
            row.third = static_cast<uint64_t>(relation.model) | (static_cast<uint64_t>(relation.relation) << SHIFT);
            const KiottySpecStatus pushed = push(cursor.value, row);
            if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
        }
    }
    return cursor;
}

CursorIdResult Finder::tags(ItemHandle item)
{
    const auto cursor = _cursors.open(CursorKind::TAG);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    for (const TagRecord& tag : _w.items.tagsOf(item)) {
        CursorRow row;
        row.first = packHandle(tag.item);
        row.second = static_cast<uint64_t>(tag.model) | (static_cast<uint64_t>(tag.tag) << SHIFT);
        row.third = tag.value;
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::checkpoints(uint32_t step)
{
    const auto cursor = _cursors.open(CursorKind::CHECKPOINT);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    for (uint32_t sequence = 1; sequence <= _w.checkpoints.latest(); ++sequence) {
        const CheckpointRecord* record = _w.checkpoints.at(sequence);
        if (record == nullptr || (step != 0 && static_cast<uint32_t>(record->step) != step)) { continue; }
        CursorRow row;
        row.number = sequence;
        const KiottySpecStatus pushed = push(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult Finder::stepRuns()
{
    const auto cursor = _cursors.open(CursorKind::STEP_RUN);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    CursorRow run;
    uint32_t step = 0;
    for (uint32_t sequence = 1; sequence <= _w.checkpoints.latest() + 1; ++sequence) {
        const CheckpointRecord* record = sequence <= _w.checkpoints.latest() ? _w.checkpoints.at(sequence) : nullptr;
        if (sequence <= _w.checkpoints.latest() && record == nullptr) { continue; }
        const uint32_t next = record == nullptr ? 0 : static_cast<uint32_t>(record->step);
        if (step != 0 && next != step) {
            const KiottySpecStatus pushed = push(cursor.value, run);
            if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(cursor.value, pushed); }
        }
        if (record == nullptr) { break; }
        if (next != step) {
            run = CursorRow{};
            run.first = sequence;
            step = next;
        }
        run.second = sequence;
        run.third = step | (static_cast<uint64_t>(record->state) << SHIFT);
        ++run.number;
    }
    return cursor;
}

Result<Position, KiottySpecStatus> Finder::position()
{
    Position position;
    position.open_change_requests = _checker.openChangeRequestCount();
    const uint32_t latest = _w.checkpoints.latest();
    const CheckpointRecord* record = _w.checkpoints.at(latest);
    if (record == nullptr) {
        position.next_step = static_cast<uint32_t>(entity::Step::CHANGE_REQUEST_REPORTED);
        return Result<Position, KiottySpecStatus>::success(position);
    }
    position.current = latest;
    position.step = static_cast<uint32_t>(record->step);
    position.state = static_cast<uint32_t>(record->state);
    if (record->state == CheckpointState::RUNNING) {
        const auto writes = _w.checkpoints.countWrites(latest);
        if (!writes) { return Result<Position, KiottySpecStatus>::failure(writes.code); }
        position.pending_writes = writes.value;
    }
    if (record->state != CheckpointState::DONE) {
        position.next_step = position.step;
    } else if (record->step == entity::Step::CHANGE_REQUEST_REPORTED) {
        // 변경 요청을 받은 뒤에는 가장 상류의 진입 단계에서 흐름을 다시 시작한다 (3.4).
        const uint32_t entry = _checker.earliestEntryStep();
        position.next_step = entry != 0 ? entry : static_cast<uint32_t>(entity::Step::REQUIREMENT_DISCUSSING);
    } else if (position.step < entity::STEP_COUNT) {
        position.next_step = position.step + 1;
    } else {
        position.next_step = position.open_change_requests > 0 ? static_cast<uint32_t>(entity::Step::CHANGE_REQUEST_REPORTED) : 0;
    }
    return Result<Position, KiottySpecStatus>::success(position);
}

}
