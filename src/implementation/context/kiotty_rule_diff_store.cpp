#include "implementation/context/kiotty_rule_diff_store.h"

#include "implementation/core/kiotty_checked.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t TEXT_SLOT_COUNT = 2;

}

RuleDiffStore::RuleDiffStore()
    : _rows(MAX_RULE_DIFF_ROWS), _texts(BODY_SLOT_BYTES * TEXT_SLOT_COUNT),
      _segments(MAX_LINE_SEGMENTS), _moves(LINE_MOVE_CELLS), _lengths(LINE_LENGTH_CELLS),
      _starts(LINE_START_CELLS)
{}

Result<Span<char>, KiottySpecStatus> RuleDiffStore::textOf(TextSource source)
{
    const Result<size_t, KiottySpecStatus> start =
        checkedMultiply(static_cast<size_t>(source), BODY_SLOT_BYTES);
    if (!start) { return Result<Span<char>, KiottySpecStatus>::failure(start.code); }
    return viewVector(_texts).subspan(start.value, BODY_SLOT_BYTES);
}

LineWork RuleDiffStore::lineWork()
{
    return LineWork {viewVector(_moves), viewVector(_lengths), viewVector(_starts)};
}

}
