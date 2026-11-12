#include "implementation/context/kiotty_cursor_pool.h"

#include "implementation/core/kiotty_checked.h"

namespace kiotty::spec::implementation {

namespace {

constexpr uint32_t GENERATION_SHIFT = 32;
constexpr uint64_t INDEX_MASK = 0xFFFFFFFFu;

CursorId encode(Handle handle);
Span<CursorRow> selectRows(Span<CursorRow> rows, size_t index, size_t width);

}

CursorPool::CursorPool(size_t max_cursors, size_t max_rows, size_t total_rows)
    : _slots(max_cursors), _rows(total_rows), _max_rows(max_rows)
{}

CursorIdResult CursorPool::open(CursorKind kind)
{
    const Handle handle = _slots.acquire();
    const bool is_valid = handle.isValid();
    if (!is_valid) { return CursorIdResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    const auto slot = _slots.get(handle);
    *slot.value = CursorSlot{0, 0, kind};
    return CursorIdResult::success(encode(handle));
}

KiottySpecStatus CursorPool::push(CursorId cursor, const CursorRow &row)
{
    const Handle handle = decode(cursor);
    const auto slot = _slots.get(handle);
    if (!slot) { return slot.code; }
    CursorSlot& state = *slot.value;
    const Result<CursorRow&, KiottySpecStatus> target = rowsOf(handle).at(state.length);
    if (!target) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    *target.value = row;
    ++state.length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

CursorRowsResult CursorPool::fetch(CursorId cursor, CursorKind kind, size_t max_count)
{
    const Handle handle = decode(cursor);
    const auto slot = _slots.get(handle);
    if (!slot) { return CursorRowsResult::failure(slot.code); }
    CursorSlot& state = *slot.value;
    if (state.kind != kind) {
        return CursorRowsResult::failure(KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE);
    }
    const size_t left = state.length - state.position;
    const size_t count = left < max_count ? left : max_count;
    const Result<Span<CursorRow>, KiottySpecStatus> page =
        rowsOf(handle).subspan(state.position, count);
    if (!page) { return CursorRowsResult::failure(page.code); }
    state.position += count;
    return CursorRowsResult::success(page.value);
}

Result<size_t, KiottySpecStatus> CursorPool::total(CursorId cursor)
{
    const auto slot = _slots.get(decode(cursor));
    if (!slot) { return Result<size_t, KiottySpecStatus>::failure(slot.code); }
    return Result<size_t, KiottySpecStatus>::success((*slot.value).length);
}

KiottySpecStatus CursorPool::close(CursorId cursor) { return _slots.release(decode(cursor)); }

Result<CursorSpace, KiottySpecStatus> CursorPool::space(CursorId cursor, CursorKind kind)
{
    const Handle handle = decode(cursor);
    const auto slot = _slots.get(handle);
    if (!slot) { return Result<CursorSpace, KiottySpecStatus>::failure(slot.code); }
    const bool is_other_kind = (*slot.value).kind != kind;
    if (is_other_kind) {
        return Result<CursorSpace, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE);
    }
    const CursorSpace cursor_space {rowsOf(handle), handle.index};
    return Result<CursorSpace, KiottySpecStatus>::success(cursor_space);
}

Handle CursorPool::decode(CursorId cursor)
{
    const uint64_t index = cursor & INDEX_MASK;
    if (index == 0) { return Handle{Handle::INVALID_INDEX, 0}; }
    return Handle { static_cast<uint32_t>(index - 1),
                    static_cast<uint32_t>(cursor >> GENERATION_SHIFT) };
}

Span<CursorRow> CursorPool::rowsOf(Handle handle)
{
    return selectRows(viewVector(_rows), handle.index, _max_rows);
}

namespace {

CursorId encode(Handle handle)
{
    return (static_cast<uint64_t>(handle.generation) << GENERATION_SHIFT) |
           (static_cast<uint64_t>(handle.index) + 1);
}

Span<CursorRow> selectRows(Span<CursorRow> rows, size_t index, size_t width)
{
    const Result<size_t, KiottySpecStatus> offset = checkedMultiply(index, width);
    if (!offset) { return Span<CursorRow>(); }
    const Result<Span<CursorRow>, KiottySpecStatus> window = rows.subspan(offset.value, width);
    return window.value;
}

}

}
