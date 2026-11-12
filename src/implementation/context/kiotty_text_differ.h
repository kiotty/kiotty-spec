#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/context/kiotty_text_diff_store.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct TextPiece {
    Span<const char> text {};
    uint32_t change {0};
};

struct TextPair {
    Span<const char> before {};
    Span<const char> after {};
};

static_assert(sizeof(TextPiece) == 24, "unexpected padding");
static_assert(sizeof(TextPair) == 32, "unexpected padding");

class TextDiffer
{
public:
    TextDiffer(Workspace& workspace, CursorPool& cursors, TextDiffStore& store)
        : _workspace(workspace), _cursors(cursors), _store(store)
    {}

    CursorIdResult open(entity::ItemHandle item, uint32_t field, DiffRange range);
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
