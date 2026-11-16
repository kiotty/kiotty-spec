#pragma once

#include "implementation/domain/entity/kiotty_query_records.h"
#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
#include "implementation/domain/usecases/cursor/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

using StepRunResult = Result<CursorRow, KiottySpecStatus>;

class HistoryFinder
{
public:
    HistoryFinder(Workspace& workspace, CursorPool& cursors)
        : _workspace(workspace), _cursors(cursors)
    {}

    CursorIdResult checkpoints(uint32_t step);
    CursorIdResult stepRuns();
    Result<entity::Step, KiottySpecStatus> latestStep();
    CursorIdResult listUnmet(Span<const entity::UnmetCondition> unmet);
    Result<entity::Position, KiottySpecStatus> locate(entity::ChangeRequestSummary requests);

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    StepRunResult addToStepRun(CursorId cursor, uint32_t sequence, const CursorRow& run);
    KiottySpecStatus pushStepRun(CursorId cursor, const CursorRow& run);
    uint64_t countWrites(uint32_t sequence);

    Workspace& _workspace;
    CursorPool& _cursors;
};

}
