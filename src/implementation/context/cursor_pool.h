#pragma once

#include "implementation/core/lazy_pool.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/domain/entity/records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

enum class CursorKind : uint8_t { NONE, ITEM, LINK, FILE, CHECKPOINT, CHANGE_REQUEST, RESULT, MILESTONE_ENTRY, STEP_RUN, ID };

// 커서가 가리키는 행의 키. 페이지를 채울 때 이 키로 레코드를 다시 읽는다.
struct CursorRow {
    IdText key;
    IdText other;
    int64_t number {0};
};

struct CursorSlot {
    CursorKind kind {CursorKind::NONE};
    size_t length {0};
    size_t position {0};
};

using CursorId = uint64_t;
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
    size_t capacity() const { return _slots.capacity(); }

private:
    static Handle decode(CursorId cursor);
    Span<CursorRow> rowsOf(Handle handle);

    LazyPool<CursorSlot> _slots;
    std::vector<CursorRow> _rows;
    size_t _maxRows;
};

}
