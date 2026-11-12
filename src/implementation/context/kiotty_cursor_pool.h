#pragma once

#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

enum class CursorKind : uint8_t {
    NONE,
    ITEM,
    TAG,
    RELATION,
    CHECKPOINT,
    STEP_RUN,
    ID,
    ITEM_DIFF,
    RELATION_DIFF,
    TEXT_DIFF,
    UNMET_CONDITION
};

struct CursorRow {
    uint64_t first {0};
    uint64_t second {0};
    uint64_t third {0};
    uint64_t fourth {0};
    int64_t number {0};
};

static_assert(sizeof(CursorRow) == 40, "unexpected padding");

struct CursorSlot {
    size_t length {0};
    size_t position {0};
    CursorKind kind {CursorKind::NONE};
};

static_assert(sizeof(CursorSlot) == 24, "unexpected padding");

using CursorId = uint64_t;

struct CursorSpace {
    Span<CursorRow> rows {};
    CursorSlot* slot {nullptr};
};

static_assert(sizeof(CursorSpace) == 24, "unexpected padding");

using CursorIdResult = Result<CursorId, KiottySpecStatus>;
using CursorRowsResult = Result<Span<const CursorRow>, KiottySpecStatus>;

class CursorPool {
public:
    CursorPool(size_t max_cursors, size_t max_rows, size_t total_rows);

    CursorIdResult open(CursorKind kind);
    KiottySpecStatus push(CursorId cursor, const CursorRow &row);
    CursorRowsResult fetch(CursorId cursor, CursorKind kind, size_t max_count);
    Result<size_t, KiottySpecStatus> total(CursorId cursor);
    KiottySpecStatus close(CursorId cursor);
    Result<CursorSpace, KiottySpecStatus> space(CursorId cursor, CursorKind kind);
    size_t capacity() const { return _slots.capacity(); }

private:
    static Handle decode(CursorId cursor);
    Span<CursorRow> rowsOf(Handle handle);

    LazyPool<CursorSlot> _slots;
    std::vector<CursorRow> _rows;
    size_t _max_rows;
};

}
