#pragma once

#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
#include "implementation/domain/usecases/cursor/kiotty_cursor_rows.h"
#include "implementation/domain/entity/kiotty_diff_records.h"
#include "implementation/domain/usecases/diff/kiotty_item_diff_query.h"
#include "implementation/domain/usecases/diff/kiotty_past_reader.h"
#include "implementation/domain/usecases/diff/kiotty_relation_diff_query.h"
#include "implementation/data/datasource/diff/kiotty_text_diff_store.h"
#include "implementation/domain/usecases/diff/kiotty_text_differ.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class Differ
{
public:
    Differ(Workspace& workspace, CursorPool& cursors, TextDiffStore& store)
        : _workspace(workspace), _cursors(cursors), _store(store)
    {}

    Result<entity::DiffRange, KiottySpecStatus> range(uint32_t from, uint32_t to);
    Result<uint32_t, KiottySpecStatus> point(uint32_t checkpoint);

    KiottySpecStatus summarize(entity::DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                               entity::KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    CursorIdResult items(entity::DiffRange range, uint32_t kind_mask, uint32_t field_mask);
    CursorIdResult relations(entity::DiffRange range, uint32_t kind_mask);

    PastReader past() { return PastReader(_workspace); }
    TextDiffer text() { return TextDiffer(_workspace, _cursors, _store); }

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    KiottySpecStatus countItems(entity::DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                                entity::KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countRelations(entity::DiffRange range, uint32_t kind_mask,
                                    entity::KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countDecisions(entity::DiffRange range, uint32_t kind_mask,
                                    entity::KindCounts (&counts)[entity::ITEM_KIND_COUNT]);

    Workspace& _workspace;
    CursorPool& _cursors;
    TextDiffStore& _store;
};

entity::ItemDiffRow unpackItemDiff(const CursorRow& row);
entity::RelationDiffRow unpackRelationDiff(const CursorRow& row);

}
