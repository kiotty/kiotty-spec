#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
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
    entity::ItemKind kind {};
    uint32_t tag {};
    uint64_t mask {};
};

class Finder
{
public:
    Finder(Workspace& workspace, CursorPool& cursors, Checker& checker, size_t max_items)
        : _workspace(workspace), _cursors(cursors), _checker(checker), _visited(max_items, 0),
          _queue(max_items, 0)
    {
    }

    CursorIdResult items(uint32_t kind, Span<const TagQuery> queries);
    CursorIdResult children(entity::ItemHandle parent);
    CursorIdResult impact(entity::ItemHandle item, uint32_t depth);
    CursorIdResult relations(entity::ItemHandle item, KiottySpecDirection direction);
    CursorIdResult relationsByModel(entity::ItemKind kind, uint32_t relation);
    CursorIdResult tags(entity::ItemHandle item);

    HistoryFinder history() { return HistoryFinder(_workspace, _cursors, _checker); }

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    bool isSelected(entity::ItemHandle handle, uint32_t kind, Span<const TagQuery> queries);
    KiottySpecStatus pushRelationsFrom(CursorId cursor, entity::ItemHandle from,
                                       entity::ItemKind kind, uint32_t relation_index);

    Workspace& _workspace;
    CursorPool& _cursors;
    Checker& _checker;
    std::vector<uint8_t> _visited;
    std::vector<uint32_t> _queue;
};

uint64_t packHandle(entity::ItemHandle handle);
entity::ItemHandle unpackHandle(uint64_t value);

}
