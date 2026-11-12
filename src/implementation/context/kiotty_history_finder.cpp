#include "implementation/context/kiotty_history_finder.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;
using entity::CheckpointState;

constexpr uint32_t SHIFT = 32;

}

CursorIdResult HistoryFinder::checkpoints(uint32_t step)
{
    const auto cursor = _cursors.open(CursorKind::CHECKPOINT);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const uint32_t latest = _workspace.checkpoints.latest();
    for (uint32_t sequence = 1; sequence <= latest; ++sequence) {
        const CheckpointRecord* record = _workspace.checkpoints.at(sequence);
        if (record == nullptr || (step != 0 && static_cast<uint32_t>(record->step) != step)) {
            continue;
        }
        CursorRow row {};
        row.number = sequence;
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

CursorIdResult HistoryFinder::stepRuns()
{
    const auto cursor = _cursors.open(CursorKind::STEP_RUN);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    CursorRow run {};
    uint32_t step = 0;
    const uint32_t latest = _workspace.checkpoints.latest();
    for (uint32_t sequence = 1; sequence <= latest; ++sequence) {
        const CheckpointRecord* record = _workspace.checkpoints.at(sequence);
        if (record == nullptr) { continue; }
        const uint32_t next = static_cast<uint32_t>(record->step);
        const bool is_new_run = next != step;
        const KiottySpecStatus pushed =
            is_new_run ? pushStepRun(cursor.value, step, run) : KIOTTY_SPEC_STATUS_SUCCESS;
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
        if (is_new_run) {
            run = CursorRow {};
            run.first = sequence;
            step = next;
        }
        run.second = sequence;
        run.third = step | (static_cast<uint64_t>(record->state) << SHIFT);
        ++run.number;
    }
    const KiottySpecStatus pushed = pushStepRun(cursor.value, step, run);
    if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    return cursor;
}

CursorIdResult HistoryFinder::unmetConditions()
{
    const CheckpointRecord* record = _workspace.checkpoints.at(_workspace.checkpoints.latest());
    if (record == nullptr) {
        return CursorIdResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    const auto cursor = _cursors.open(CursorKind::UNMET_CONDITION);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    for (const UnmetCondition& unmet : _checker.collect(record->step)) {
        CursorRow row {};
        row.first = packIdWord(unmet.id, 0);
        row.second = packIdWord(unmet.id, PACKED_ID_WORD_BYTES);
        row.third = static_cast<uint64_t>(unmet.condition);
        const KiottySpecStatus pushed = writer.pushRow(cursor.value, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    }
    return cursor;
}

Result<Position, KiottySpecStatus> HistoryFinder::position()
{
    Position position {};
    position.open_change_requests = _checker.openChangeRequestCount();
    const uint32_t latest = _workspace.checkpoints.latest();
    const CheckpointRecord* record = _workspace.checkpoints.at(latest);
    if (record == nullptr) {
        position.next_step = static_cast<uint32_t>(entity::Step::CHANGE_REQUEST_REPORTED);
        return Result<Position, KiottySpecStatus>::success(position);
    }
    position.current = latest;
    position.step = static_cast<uint32_t>(record->step);
    position.state = static_cast<uint32_t>(record->state);
    if (record->state == CheckpointState::RUNNING) {
        position.pending_writes = countWrites(latest);
    }
    if (record->state != CheckpointState::DONE) {
        position.next_step = position.step;
    } else if (record->step == entity::Step::CHANGE_REQUEST_REPORTED) {
        const uint32_t entry = _checker.earliestEntryStep();
        position.next_step =
            entry != 0 ? entry : static_cast<uint32_t>(entity::Step::REQUIREMENT_DISCUSSING);
    } else if (position.step < entity::STEP_COUNT) {
        position.next_step = position.step + 1;
    } else {
        position.next_step = position.open_change_requests > 0
                                 ? static_cast<uint32_t>(entity::Step::CHANGE_REQUEST_REPORTED)
                                 : 0;
    }
    return Result<Position, KiottySpecStatus>::success(position);
}

KiottySpecStatus HistoryFinder::pushStepRun(CursorId cursor, uint32_t step, const CursorRow& run)
{
    if (step == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return makeCursorWriter().pushRow(cursor, run);
}

uint64_t HistoryFinder::countWrites(uint32_t sequence)
{
    const ItemTables& items = _workspace.items.tables();
    return items.versions.countAt(sequence) + items.tag_versions.countAt(sequence) +
           _workspace.relations.versions().countAt(sequence) +
           _workspace.decisions.versions().countAt(sequence);
}

}
