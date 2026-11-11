#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct Position {
    uint32_t current {0};
    uint32_t step {0};
    uint32_t state {0};
    uint32_t next_step {0};
    uint64_t pending_writes {0};
    uint64_t open_change_requests {0};
};

class HistoryFinder
{
public:
    HistoryFinder(Workspace& workspace, CursorPool& cursors, Checker& checker) : _w(workspace), _cursors(cursors), _checker(checker) {}

    CursorIdResult checkpoints(uint32_t step);
    CursorIdResult stepRuns();
    CursorIdResult unmetConditions();
    Result<Position, KiottySpecStatus> position();

private:
    Workspace& _w;
    CursorPool& _cursors;
    Checker& _checker;
};

}
