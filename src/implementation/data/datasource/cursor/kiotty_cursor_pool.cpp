#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"

#include "implementation/core/kiotty_checked.h"

namespace kiotty::spec::implementation {

namespace {

Span<CursorRow> selectRows(Span<CursorRow> rows, size_t index, size_t width);

}

CursorPool::CursorPool(size_t max_cursors, size_t max_rows, size_t total_rows)
    : _slots(max_cursors), _rows(total_rows), _max_rows(max_rows)
{}

CursorIdResult CursorPool::open(CursorKind kind)
{
    const entity::CursorHandle handle = _slots.acquire();
    const bool is_valid = handle.isValid();
    if (!is_valid) { return CursorIdResult::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
    const auto slot = _slots.get(handle);
    *slot.value = CursorSlot{0, 0, kind};
    return CursorIdResult::success(entity::encodeCursor(handle));
}

KiottySpecStatus CursorPool::push(CursorId cursor, const CursorRow &row)
{
    const entity::CursorHandle handle = entity::decodeCursor(cursor);
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
    const entity::CursorHandle handle = entity::decodeCursor(cursor);
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
    const Result<CursorSlot&, KiottySpecStatus> slot = _slots.get(entity::decodeCursor(cursor));
    if (!slot) { return Result<size_t, KiottySpecStatus>::failure(slot.code); }
    return Result<size_t, KiottySpecStatus>::success((*slot.value).length);
}

KiottySpecStatus CursorPool::close(CursorId cursor)
{
    return _slots.release(entity::decodeCursor(cursor));
}

Result<entity::CursorHandle, KiottySpecStatus> CursorPool::locate(CursorId cursor,
                                                                 CursorKind kind)
{
    using HandleResult = Result<entity::CursorHandle, KiottySpecStatus>;
    const entity::CursorHandle handle = entity::decodeCursor(cursor);
    const auto slot = _slots.get(handle);
    if (!slot) { return HandleResult::failure(slot.code); }
    const bool is_other_kind = (*slot.value).kind != kind;
    if (is_other_kind) { return HandleResult::failure(KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE); }
    return HandleResult::success(handle);
}


Span<CursorRow> CursorPool::rowsOf(Handle handle)
{
    return selectRows(viewVector(_rows), handle.index, _max_rows);
}

namespace {

Span<CursorRow> selectRows(Span<CursorRow> rows, size_t index, size_t width)
{
    const Result<size_t, KiottySpecStatus> offset = checkedMultiply(index, width);
    if (!offset) { return Span<CursorRow>(); }
    const Result<Span<CursorRow>, KiottySpecStatus> window = rows.subspan(offset.value, width);
    return window.value;
}

}

}
