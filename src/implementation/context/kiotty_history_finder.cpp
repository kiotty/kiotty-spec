#include "implementation/context/kiotty_history_finder.h"

#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;

constexpr uint32_t SHIFT = 32;

// 이 체크포인트에 묶인 버전 수 (항목 · 태그 · 관계 · 결정 기록)
uint64_t countWrites(Workspace& workspace, uint32_t sequence)
{
    const ItemTables& items = workspace.items.tables();
    return items.versions.countAt(sequence) + items.tag_versions.countAt(sequence) + workspace.relations.versions().countAt(sequence) +
           workspace.decisions.versions().countAt(sequence);
}

}

CursorIdResult HistoryFinder::checkpoints(uint32_t step)
{
    const auto cursor = _cursors.open(CursorKind::CHECKPOINT);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    for (uint32_t sequence = 1; sequence <= _w.checkpoints.latest(); ++sequence) {
        const CheckpointRecord* record = _w.checkpoints.at(sequence);
        if (record == nullptr || (step != 0 && static_cast<uint32_t>(record->step) != step)) { continue; }
        CursorRow row {};
        row.number = sequence;
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return closeWith(_cursors, cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult HistoryFinder::stepRuns()
{
    const auto cursor = _cursors.open(CursorKind::STEP_RUN);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    CursorRow run {};
    uint32_t step = 0;
    for (uint32_t sequence = 1; sequence <= _w.checkpoints.latest() + 1; ++sequence) {
        const CheckpointRecord* record = sequence <= _w.checkpoints.latest() ? _w.checkpoints.at(sequence) : nullptr;
        if (sequence <= _w.checkpoints.latest() && record == nullptr) { continue; }
        const uint32_t next = record == nullptr ? 0 : static_cast<uint32_t>(record->step);
        if (step != 0 && next != step) {
            const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, run);
            if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return closeWith(_cursors, cursor.value, pushed); }
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

CursorIdResult HistoryFinder::unmetConditions()
{
    const CheckpointRecord* record = _w.checkpoints.at(_w.checkpoints.latest());
    if (record == nullptr) { return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT)); }
    const auto cursor = _cursors.open(CursorKind::UNMET_CONDITION);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    for (const UnmetCondition& unmet : _checker.collect(record->step)) {
        CursorRow row {};
        row.first = packIdWord(unmet.id, 0);
        row.second = packIdWord(unmet.id, PACKED_ID_WORD_BYTES);
        row.third = static_cast<uint64_t>(unmet.condition);
        const KiottySpecStatus pushed = pushRow(_cursors, _w.failure, cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return closeWith(_cursors, cursor.value, pushed); }
    }
    return cursor;
}

Result<Position, KiottySpecStatus> HistoryFinder::position()
{
    Position position {};
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
    if (record->state == CheckpointState::RUNNING) { position.pending_writes = countWrites(_w, latest); }
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
