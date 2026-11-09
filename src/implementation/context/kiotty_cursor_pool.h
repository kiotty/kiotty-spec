#pragma once

#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

enum class CursorKind : uint8_t { NONE, ITEM, TAG, RELATION, CHECKPOINT, STEP_RUN, ID, ITEM_DIFF, RELATION_DIFF, TEXT_DIFF };

// 커서가 가리키는 행의 키 (40 바이트). 페이지를 채울 때 이 키로 레코드를 다시 읽거나 그대로 옮긴다.
// 항목 · ID: first = 항목 핸들. 태그: first = 항목 핸들, second = model | tag << 32, third = value.
// 관계: first = from, second = to, third = model | relation << 32. 체크포인트: number = sequence.
// 단계 구간: first = 처음 sequence, second = 마지막 sequence, third = step | state << 32, number = 개수.
struct CursorRow {
    uint64_t first {0};
    uint64_t second {0};
    uint64_t third {0};
    uint64_t fourth {0};
    int64_t number {0};
};

static_assert(sizeof(CursorRow) == 40, "unexpected padding");

struct CursorSlot {
    CursorKind kind {CursorKind::NONE};
    size_t length {0};
    size_t position {0};
};

using CursorId = uint64_t;

// 커서 하나의 슬롯과 행 영역. 글자 diff 처럼 행 영역을 바이트로 쓰는 커서가 쓴다.
struct CursorSpace {
    CursorSlot* slot {nullptr};
    Span<CursorRow> rows;
};
using CursorIdResult = Result<CursorId, KiottySpecStatus>;
using CursorRowsResult = Result<Span<const CursorRow>, KiottySpecStatus>;

// 커서 슬롯과 행 영역을 열 때 모두 확보한다. 커서 ID 는 {index, generation} 이며 0 은 늘 무효다.
class CursorPool {
public:
    CursorPool(size_t maxCursors, size_t maxRows);

    CursorIdResult open(CursorKind kind);
    KiottySpecStatus push(CursorId cursor, const CursorRow &row);
    CursorRowsResult fetch(CursorId cursor, CursorKind kind, size_t maxCount);
    Result<size_t, KiottySpecStatus> total(CursorId cursor);
    KiottySpecStatus close(CursorId cursor);
    Result<CursorSpace, KiottySpecStatus> space(CursorId cursor, CursorKind kind);
    size_t capacity() const { return _slots.capacity(); }

private:
    static Handle decode(CursorId cursor);
    Span<CursorRow> rowsOf(Handle handle);

    LazyPool<CursorSlot> _slots;
    std::vector<CursorRow> _rows;
    size_t _maxRows;
};

}
