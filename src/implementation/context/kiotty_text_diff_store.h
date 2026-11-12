#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_diff.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

constexpr size_t TEXT_SLOT_BYTES = MAX_DIFF_TEXT_BYTES + 1;
constexpr size_t TEXT_PAIR_BYTES = TEXT_SLOT_BYTES * 2;
constexpr size_t DIFF_TABLE_CELLS = TEXT_SLOT_BYTES * TEXT_SLOT_BYTES;
constexpr size_t MAX_TEXT_SEGMENTS = MAX_DIFF_TEXT_BYTES * 2 + 1;

class TextDiffStore
{
public:
    explicit TextDiffStore(size_t max_cursors);

    Result<Span<char>, KiottySpecStatus> textOf(size_t cursor_index, TextSource source);
    Span<uint8_t> table() { return viewVector(_table); }
    Span<TextSegment> segments() { return viewVector(_segments); }

private:
    std::vector<char> _texts;
    std::vector<uint8_t> _table;
    std::vector<TextSegment> _segments;
};

}
