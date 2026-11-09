#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct TextPiece {
    const char* text {nullptr};
    size_t length {0};
    uint32_t change {0};
};

// 제목 · 요약의 글자 diff 커서 (설계서 v1 5.4). Differ 의 세부 클래스 (개발수칙 4.19). LCS 표는 작업 버퍼를 빌린다.
class TextDiffer
{
public:
    TextDiffer(Workspace& workspace, CursorPool& cursors, TextBuffer& work) : _w(workspace), _cursors(cursors), _work(work) {}

    CursorIdResult open(entity::ItemHandle item, uint32_t field, DiffRange range);
    Result<TextPiece, KiottySpecStatus> next(CursorId cursor);

private:
    Workspace& _w;
    CursorPool& _cursors;
    TextBuffer& _work;
};

}
