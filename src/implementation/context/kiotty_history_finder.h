#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct Position {
    uint64_t pending_writes {0};
    uint64_t open_change_requests {0};
    uint32_t current {0};
    uint32_t step {0};
    uint32_t state {0};
    uint32_t next_step {0};
};

static_assert(sizeof(Position) == 32, "unexpected padding");

using StepRunResult = Result<CursorRow, KiottySpecStatus>;

class HistoryFinder
{
public:
    HistoryFinder(Workspace& workspace, CursorPool& cursors, Checker& checker)
        : _workspace(workspace), _cursors(cursors), _checker(checker)
    {}

    CursorIdResult checkpoints(uint32_t step);
    CursorIdResult stepRuns();
    CursorIdResult unmetConditions();
    Result<Position, KiottySpecStatus> position();

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    StepRunResult addToStepRun(CursorId cursor, uint32_t sequence, const CursorRow& run);
    KiottySpecStatus pushStepRun(CursorId cursor, const CursorRow& run);
    uint64_t countWrites(uint32_t sequence);

    Workspace& _workspace;
    CursorPool& _cursors;
    Checker& _checker;
};

}
