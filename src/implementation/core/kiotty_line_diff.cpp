#include "kiotty_line_diff.h"

#include "kiotty_checked.h"

namespace kiotty::spec::implementation {

namespace {

using CountResult = Result<size_t, KiottySpecStatus>;

enum class LineMove : uint8_t { SAME = 0, REMOVE = 1, ADD = 2 };

constexpr char NEW_LINE = '\n';
constexpr size_t MAX_SEGMENT_OFFSET = UINT16_MAX;

struct Lines {
    Span<const char> text {};
    Span<uint16_t> starts {};
    size_t count {0};
};

struct LinePair {
    const Lines& before;
    const Lines& after;
};

struct Position {
    size_t before {0};
    size_t after {0};
};

static_assert(sizeof(Lines) == 40, "unexpected padding");
static_assert(sizeof(LinePair) == 16, "unexpected padding");
static_assert(sizeof(Position) == 16, "unexpected padding");

using LinesResult = Result<Lines, KiottySpecStatus>;

LinesResult splitLines(Span<const char> text, Span<uint16_t> starts);
KiottySpecStatus checkWork(const LineWork& work, LinePair pair);
void fillMoves(LinePair pair, const LineWork& work);
void fillRow(LinePair pair, size_t row, Span<uint16_t> lengths, Span<uint8_t> moves);
bool isSameLine(LinePair pair, size_t before, size_t after);
Span<const char> lineOf(const Lines& lines, size_t index);
LineMove pickMove(uint16_t down, uint16_t right);
CountResult walkMoves(LinePair pair, Span<const uint8_t> moves, Span<TextSegment> out);
LineMove readMove(LinePair pair, Span<const uint8_t> moves, Position position);
TextSegment makeSegment(LinePair pair, Position position, LineMove move);
TextChange changeOf(LineMove move);
CountResult appendSegment(Span<TextSegment> out, size_t count, TextSegment segment);
Position advance(Position position, LineMove move);

}

Result<size_t, KiottySpecStatus> diffLines(Span<const char> before, Span<const char> after,
                                           const LineWork& work, Span<TextSegment> out)
{
    const auto before_starts = work.starts.subspan(0, MAX_DIFF_LINES + 1);
    const auto after_starts = work.starts.subspan(MAX_DIFF_LINES + 1, MAX_DIFF_LINES + 1);
    const bool has_starts = before_starts && after_starts;
    if (!has_starts) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const LinesResult before_lines = splitLines(before, before_starts.value);
    if (!before_lines) { return CountResult::failure(before_lines.code); }
    const LinesResult after_lines = splitLines(after, after_starts.value);
    if (!after_lines) { return CountResult::failure(after_lines.code); }
    const LinePair pair {before_lines.value, after_lines.value};
    const KiottySpecStatus checked = checkWork(work, pair);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(checked); }
    fillMoves(pair, work);
    return walkMoves(pair, work.moves, out);
}

namespace {

LinesResult splitLines(Span<const char> text, Span<uint16_t> starts)
{
    const size_t text_length = text.length();
    const size_t capacity = starts.length();
    const bool is_too_long = text_length > MAX_SEGMENT_OFFSET || capacity == 0;
    if (is_too_long) { return LinesResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    Lines lines {text, starts, 0};
    starts[0] = 0;
    for (size_t index = 0; index < text_length; ++index) {
        const bool is_line_end = text[index] == NEW_LINE || index + 1 == text_length;
        if (!is_line_end) { continue; }
        const bool is_full = lines.count + 1 >= capacity;
        if (is_full) { return LinesResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
        ++lines.count;
        starts[lines.count] = static_cast<uint16_t>(index + 1);
    }
    return LinesResult::success(lines);
}

KiottySpecStatus checkWork(const LineWork& work, LinePair pair)
{
    const Result<size_t, KiottySpecStatus> moves =
        checkedMultiply(pair.before.count, pair.after.count);
    const Result<size_t, KiottySpecStatus> lengths =
        checkedMultiply(pair.after.count + 1, LINE_ROW_COUNT);
    const bool has_sizes = moves && lengths;
    if (!has_sizes) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const bool has_moves = work.moves.length() >= moves.value;
    const bool has_lengths = work.lengths.length() >= lengths.value;
    const bool is_enough = has_moves && has_lengths;
    return is_enough ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
}

void fillMoves(LinePair pair, const LineWork& work)
{
    const size_t row_cells = pair.after.count + 1;
    const Span<uint16_t> next = work.lengths.subspan(0, row_cells).value;
    const Span<uint16_t> current = work.lengths.subspan(row_cells, row_cells).value;
    for (uint16_t& length : next) { length = 0; }
    for (size_t row = pair.before.count; row > 0; --row) {
        fillRow(pair, row - 1, work.lengths, work.moves);
        next.copyFrom(current);
    }
}

void fillRow(LinePair pair, size_t row, Span<uint16_t> lengths, Span<uint8_t> moves)
{
    const size_t columns = pair.after.count;
    const size_t current = columns + 1;
    lengths[current + columns] = 0;
    const size_t row_start = checkedMultiply(row, columns).value;
    for (size_t column = columns; column > 0; --column) {
        const size_t at = column - 1;
        const bool is_same = isSameLine(pair, row, at);
        const uint16_t down = lengths[at];
        const uint16_t right = lengths[current + at + 1];
        const uint16_t diagonal = static_cast<uint16_t>(lengths[at + 1] + 1);
        const uint16_t best = down >= right ? down : right;
        const LineMove chosen = is_same ? LineMove::SAME : pickMove(down, right);
        lengths[current + at] = is_same ? diagonal : best;
        moves[row_start + at] = static_cast<uint8_t>(chosen);
    }
}

bool isSameLine(LinePair pair, size_t before, size_t after)
{
    const Span<const char> left = lineOf(pair.before, before);
    const Span<const char> right = lineOf(pair.after, after);
    const size_t length = left.length();
    const bool is_same_length = length == right.length();
    if (!is_same_length) { return false; }
    for (size_t index = 0; index < length; ++index) {
        const bool is_same_letter = left[index] == right[index];
        if (!is_same_letter) { return false; }
    }
    return true;
}

Span<const char> lineOf(const Lines& lines, size_t index)
{
    const size_t start = lines.starts[index];
    const size_t end = lines.starts[index + 1];
    const auto line = lines.text.subspan(start, end - start);
    return line ? line.value : Span<const char>();
}

LineMove pickMove(uint16_t down, uint16_t right)
{
    return down >= right ? LineMove::REMOVE : LineMove::ADD;
}

CountResult walkMoves(LinePair pair, Span<const uint8_t> moves, Span<TextSegment> out)
{
    size_t count = 0;
    Position position {};
    const size_t total = pair.before.count + pair.after.count;
    for (size_t step = 0; step < total; ++step) {
        const bool is_done =
            position.before == pair.before.count && position.after == pair.after.count;
        if (is_done) { break; }
        const LineMove move = readMove(pair, moves, position);
        const CountResult appended = appendSegment(out, count, makeSegment(pair, position, move));
        if (!appended) { return appended; }
        count = appended.value;
        position = advance(position, move);
    }
    return CountResult::success(count);
}

LineMove readMove(LinePair pair, Span<const uint8_t> moves, Position position)
{
    const bool is_before_done = position.before == pair.before.count;
    if (is_before_done) { return LineMove::ADD; }
    const bool is_after_done = position.after == pair.after.count;
    if (is_after_done) { return LineMove::REMOVE; }
    const size_t row_start = checkedMultiply(position.before, pair.after.count).value;
    return static_cast<LineMove>(moves[row_start + position.after]);
}

TextSegment makeSegment(LinePair pair, Position position, LineMove move)
{
    const bool is_after = move == LineMove::ADD;
    const Lines& lines = is_after ? pair.after : pair.before;
    const size_t index = is_after ? position.after : position.before;
    const uint16_t start = lines.starts[index];
    const uint16_t length = static_cast<uint16_t>(lines.starts[index + 1] - start);
    const TextSource source = is_after ? TextSource::AFTER : TextSource::BEFORE;
    return TextSegment {start, length, 0, changeOf(move), source};
}

TextChange changeOf(LineMove move)
{
    switch (move) {
    case LineMove::SAME: return TextChange::UNCHANGED;
    case LineMove::REMOVE: return TextChange::REMOVED;
    case LineMove::ADD: return TextChange::ADDED;
    }
    return TextChange::UNCHANGED;
}

CountResult appendSegment(Span<TextSegment> out, size_t count, TextSegment segment)
{
    const bool has_last = count > 0;
    if (has_last) {
        TextSegment& last = out[count - 1];
        const bool is_joined =
            last.change == segment.change && last.offset + last.length == segment.offset;
        if (is_joined) {
            last.length = static_cast<uint16_t>(last.length + segment.length);
            return CountResult::success(count);
        }
    }
    const auto slot = out.at(count);
    if (!slot) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    *slot.value = segment;
    return CountResult::success(count + 1);
}

Position advance(Position position, LineMove move)
{
    Position next = position;
    next.before += move == LineMove::ADD ? 0 : 1;
    next.after += move == LineMove::REMOVE ? 0 : 1;
    return next;
}

}

}
