#pragma once

#include "implementation/domain/entity/kiotty_public_handle.h"
#include "implementation/domain/entity/kiotty_query_records.h"
#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
#include "implementation/domain/usecases/cursor/kiotty_cursor_rows.h"
#include "implementation/domain/usecases/query/kiotty_history_finder.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

class Finder
{
public:
    Finder(Workspace& workspace, CursorPool& cursors, size_t max_items)
        : _workspace(workspace), _cursors(cursors), _tag_values(max_items, 0),
          _marks(max_items, 0)
    {
    }

    CursorIdResult items(uint32_t kind, Span<const entity::TagQuery> queries);
    CursorIdResult children(entity::ItemHandle parent);
    CursorIdResult impact(entity::ItemHandle item, uint32_t depth);
    CursorIdResult relations(entity::ItemHandle item, KiottySpecDirection direction);
    CursorIdResult relationsByModel(entity::ItemKind kind, uint32_t relation);
    CursorIdResult tags(entity::ItemHandle item);

    HistoryFinder history() { return HistoryFinder(_workspace, _cursors); }

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    void markKind(uint32_t kind);
    void keepTagged(entity::TagQuery query);
    void markChildren(entity::ItemHandle parent);
    void markSources(entity::ItemKind kind, uint32_t relation_index);
    KiottySpecStatus pushMarked(CursorId cursor);
    KiottySpecStatus pushRelationsFrom(CursorId cursor, uint32_t from_index,
                                       entity::ItemKind kind, uint32_t relation_index);

    Workspace& _workspace;
    CursorPool& _cursors;
    std::vector<uint64_t> _tag_values;
    std::vector<uint8_t> _marks;
};


}
