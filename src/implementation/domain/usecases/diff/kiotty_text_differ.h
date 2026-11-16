#pragma once

#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
#include "implementation/domain/usecases/cursor/kiotty_cursor_rows.h"
#include "implementation/data/datasource/diff/kiotty_text_piece.h"
#include "implementation/domain/entity/kiotty_diff_records.h"
#include "implementation/data/datasource/diff/kiotty_text_diff_store.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class TextDiffer
{
public:
    TextDiffer(Workspace& workspace, CursorPool& cursors, TextDiffStore& store)
        : _workspace(workspace), _cursors(cursors), _store(store)
    {}

    CursorIdResult open(entity::ItemHandle item, uint32_t field, entity::DiffRange range);
    Result<TextPiece, KiottySpecStatus> next(CursorId cursor);

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    Result<entity::IdText, KiottySpecStatus> readItemId(entity::ItemHandle item);
    KiottySpecStatus fillSegments(CursorId cursor, const TextPair& texts);
    KiottySpecStatus storeTexts(size_t cursor_index, const TextPair& texts);
    KiottySpecStatus pushSegments(CursorId cursor, Span<const TextSegment> segments);

    Workspace& _workspace;
    CursorPool& _cursors;
    TextDiffStore& _store;
};

}
