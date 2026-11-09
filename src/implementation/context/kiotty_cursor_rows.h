#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

// 목록 질의가 결과 행을 커서에 담을 때 쓰는 도우미. 행이 넘치면 이유를 남긴다.
inline KiottySpecStatus pushRow(CursorPool& cursors, Failure& failure, CursorId cursor, const CursorRow& row)
{
    const KiottySpecStatus pushed = cursors.push(cursor, row);
    if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        return failure.raise(pushed, {literalText("결과 행이 max_cursor_rows 를 넘음. 조건을 좁힐 것")});
    }
    return pushed;
}

// 채우던 커서를 닫고 실패를 돌려준다.
inline CursorIdResult closeWith(CursorPool& cursors, CursorId cursor, KiottySpecStatus status)
{
    cursors.close(cursor);
    return CursorIdResult::failure(status);
}

}
