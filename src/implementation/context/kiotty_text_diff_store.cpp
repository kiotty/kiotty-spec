#include "implementation/context/kiotty_text_diff_store.h"

#include "implementation/core/kiotty_checked.h"

namespace kiotty::spec::implementation {

namespace {

size_t measureTextBytes(size_t max_cursors);

}

TextDiffStore::TextDiffStore(size_t max_cursors)
    : _texts(measureTextBytes(max_cursors)), _table(DIFF_TABLE_CELLS),
      _segments(MAX_TEXT_SEGMENTS)
{}

Result<Span<char>, KiottySpecStatus> TextDiffStore::textOf(size_t cursor_index, TextSource source)
{
    using TextResult = Result<Span<char>, KiottySpecStatus>;
    const Result<size_t, KiottySpecStatus> pair_start =
        checkedMultiply(cursor_index, TEXT_PAIR_BYTES);
    if (!pair_start) { return TextResult::failure(pair_start.code); }
    const Span<char> texts = viewVector(_texts);
    const TextResult pair = texts.subspan(pair_start.value, TEXT_PAIR_BYTES);
    if (!pair) { return pair; }
    const size_t slot_start = source == TextSource::AFTER ? TEXT_SLOT_BYTES : 0;
    return pair.value.subspan(slot_start, TEXT_SLOT_BYTES);
}

namespace {

size_t measureTextBytes(size_t max_cursors)
{
    const Result<size_t, KiottySpecStatus> bytes = checkedMultiply(max_cursors, TEXT_PAIR_BYTES);
    return bytes ? bytes.value : 0;
}

}

}
