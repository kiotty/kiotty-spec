#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_text_buffer.h"
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
    TextDiffer(Workspace& workspace, CursorPool& cursors, TextBuffer& work)
        : _workspace(workspace), _cursors(cursors), _work(work)
    {}

    CursorIdResult open(entity::ItemHandle item, uint32_t field, DiffRange range);
    Result<TextPiece, KiottySpecStatus> next(CursorId cursor);

private:
    CursorWriter makeCursorWriter() { return CursorWriter(_cursors, _workspace.failure); }
    Result<entity::IdText, KiottySpecStatus> readItemId(entity::ItemHandle item);
    KiottySpecStatus fillSegments(CursorId cursor, const TextPair& texts);

    Workspace& _workspace;
    CursorPool& _cursors;
    TextBuffer& _work;
};

}
