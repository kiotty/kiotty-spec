#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/context/kiotty_item_diff_query.h"
#include "implementation/context/kiotty_past_reader.h"
#include "implementation/context/kiotty_relation_diff_query.h"
#include "implementation/context/kiotty_text_diff_store.h"
#include "implementation/context/kiotty_text_differ.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct KindCounts {
    uint32_t added {0};
    uint32_t modified {0};
    uint32_t removed {0};
    uint32_t relations_added {0};
    uint32_t relations_removed {0};
    bool has_decision_change {false};
};

static_assert(sizeof(KindCounts) == 24, "unexpected padding");

class Differ
{
public:
    Differ(Workspace& workspace, CursorPool& cursors, TextDiffStore& store)
        : _workspace(workspace), _cursors(cursors), _store(store)
    {}

    Result<DiffRange, KiottySpecStatus> range(uint64_t from, uint64_t to);
    Result<uint32_t, KiottySpecStatus> point(uint64_t checkpoint);

    KiottySpecStatus summarize(DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                               KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    CursorIdResult items(DiffRange range, uint32_t kind_mask, uint32_t field_mask);
    CursorIdResult relations(DiffRange range, uint32_t kind_mask);

    PastReader past() { return PastReader(_workspace); }
    TextDiffer text() { return TextDiffer(_workspace, _cursors, _store); }

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    KiottySpecStatus countItems(DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                                KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countRelations(DiffRange range, uint32_t kind_mask,
                                    KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countDecisions(DiffRange range, uint32_t kind_mask,
                                    KindCounts (&counts)[entity::ITEM_KIND_COUNT]);

    Workspace& _workspace;
    CursorPool& _cursors;
    TextDiffStore& _store;
};

ItemDiffRow unpackItemDiff(const CursorRow& row);
RelationDiffRow unpackRelationDiff(const CursorRow& row);

}
