#include "implementation/context/kiotty_rule_diff_store.h"

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
    const size_t slot = static_cast<size_t>(source);
    return viewVector(_texts).subspan(slot * BODY_SLOT_BYTES, BODY_SLOT_BYTES);
}

LineWork RuleDiffStore::lineWork()
{
    return LineWork {viewVector(_moves), viewVector(_lengths), viewVector(_starts)};
}

}
