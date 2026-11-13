#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"
#include "kiotty_text_diff.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DIFF_LINES = 1024;
constexpr size_t LINE_ROW_COUNT = 2;
constexpr size_t LINE_MOVE_CELLS = MAX_DIFF_LINES * MAX_DIFF_LINES;
constexpr size_t LINE_LENGTH_CELLS = (MAX_DIFF_LINES + 1) * LINE_ROW_COUNT;
constexpr size_t LINE_START_CELLS = (MAX_DIFF_LINES + 1) * LINE_ROW_COUNT;
constexpr size_t MAX_LINE_SEGMENTS = MAX_DIFF_LINES * LINE_ROW_COUNT + 1;

struct LineWork {
    Span<uint8_t> moves {};
    Span<uint16_t> lengths {};
    Span<uint16_t> starts {};
};

static_assert(sizeof(LineWork) == 48, "unexpected padding");

Result<size_t, KiottySpecStatus> diffLines(Span<const char> before, Span<const char> after,
                                           const LineWork& work, Span<TextSegment> out);

}
