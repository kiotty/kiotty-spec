#pragma once

#include "kiotty_spec.h"
#include "implementation/context/cursor_pool.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/domain/entity/item_records.h"
#include "implementation/domain/usecases/checker.h"
#include "implementation/domain/usecases/workspace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct TagQuery {
    entity::ItemKind kind;
    uint32_t tag;
    uint64_t mask;
};

struct Position {
    uint32_t current {0};
    uint32_t step {0};
    uint32_t state {0};
    uint32_t next_step {0};
    uint64_t pending_writes {0};
    uint64_t open_change_requests {0};
};

// 목록 질의 (설계서 v1 8). 결과 행의 키를 커서 풀에 담는다. 레코드는 페이지를 받을 때 다시 읽는다.
class Finder
{
public:
    Finder(Workspace& workspace, CursorPool& cursors, Checker& checker, size_t max_items)
        : _w(workspace), _cursors(cursors), _checker(checker), _visited(max_items, 0), _queue(max_items, 0)
    {
    }

    // kind 가 0 이면 모든 Kind. 조건은 AND, 조건 안의 비트는 OR (8.3)
    CursorIdResult items(uint32_t kind, Span<const TagQuery> queries);
    CursorIdResult children(entity::ItemHandle parent);
    CursorIdResult impact(entity::ItemHandle item, uint32_t depth);
    CursorIdResult relations(entity::ItemHandle item, KiottySpecDirection direction);
    CursorIdResult relationsByModel(entity::ItemKind kind, uint32_t relation);
    CursorIdResult tags(entity::ItemHandle item);
    CursorIdResult checkpoints(uint32_t step);
    CursorIdResult stepRuns();

    Result<Position, KiottySpecStatus> position();

private:
    KiottySpecStatus push(CursorId cursor, const CursorRow& row);
    CursorIdResult fail(CursorId cursor, KiottySpecStatus status);

    Workspace& _w;
    CursorPool& _cursors;
    Checker& _checker;
    std::vector<uint8_t> _visited;
    std::vector<uint32_t> _queue;
};

uint64_t packHandle(entity::ItemHandle handle);
entity::ItemHandle unpackHandle(uint64_t value);

}
