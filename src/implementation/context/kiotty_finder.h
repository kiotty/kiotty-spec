#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_history_finder.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct TagQuery {
    entity::ItemKind kind;
    uint32_t tag;
    uint64_t mask;
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

    // 체크포인트 이력과 현재 위치는 세부 클래스에 맡긴다 (개발수칙 4.19). 부른 자리에서 쓰고 버린다.
    HistoryFinder history() { return HistoryFinder(_w, _cursors, _checker); }

private:
    Workspace& _w;
    CursorPool& _cursors;
    Checker& _checker;
    std::vector<uint8_t> _visited;
    std::vector<uint32_t> _queue;
};

uint64_t packHandle(entity::ItemHandle handle);
entity::ItemHandle unpackHandle(uint64_t value);

}
