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

// 체크포인트 이력과 현재 위치 (설계서 v1 8). Finder 의 세부 클래스 (개발수칙 4.19).
class HistoryFinder
{
public:
    HistoryFinder(Workspace& workspace, CursorPool& cursors, Checker& checker) : _w(workspace), _cursors(cursors), _checker(checker) {}

    // step 이 0 이면 모든 단계
    CursorIdResult checkpoints(uint32_t step);
    CursorIdResult stepRuns();
    Result<Position, KiottySpecStatus> position();

private:
    Workspace& _w;
    CursorPool& _cursors;
    Checker& _checker;
};

}
