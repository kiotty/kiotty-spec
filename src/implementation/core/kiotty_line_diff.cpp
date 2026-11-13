#include "kiotty_line_diff.h"

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

struct SegmentList {
    Span<TextSegment> out {};
    size_t count {0};
};

struct Position {
    size_t before {0};
    size_t after {0};
};

static_assert(sizeof(Lines) == 40, "unexpected padding");
static_assert(sizeof(LinePair) == 16, "unexpected padding");
static_assert(sizeof(SegmentList) == 24, "unexpected padding");
static_assert(sizeof(Position) == 16, "unexpected padding");

Result<Lines, KiottySpecStatus> splitLines(Span<const char> text, Span<uint16_t> starts);
KiottySpecStatus checkWork(const LineWork& work, size_t before_count, size_t after_count);
void fillMoves(LinePair pair, const LineWork& work);
void fillRow(LinePair pair, size_t row, Span<uint16_t> lengths, Span<uint8_t> moves);
LineMove largerMove(uint16_t down, uint16_t right);
bool isSameLine(LinePair pair, size_t before, size_t after);
Span<const char> lineAt(const Lines& lines, size_t index);
CountResult walkMoves(LinePair pair, Span<const uint8_t> moves, Span<TextSegment> out);
LineMove readMove(LinePair pair, Span<const uint8_t> moves, Position position);
TextChange changeOf(LineMove move);
KiottySpecStatus appendLine(SegmentList& list, const Lines& lines, size_t index, TextChange change);

}

Result<size_t, KiottySpecStatus> diffLines(Span<const char> before, Span<const char> after,
                                           const LineWork& work, Span<TextSegment> out)
{
    const auto halves = work.starts.subspan(0, MAX_DIFF_LINES + 1);
    const auto rest = work.starts.subspan(MAX_DIFF_LINES + 1, MAX_DIFF_LINES + 1);
    if (!halves || !rest) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const Result<Lines, KiottySpecStatus> before_lines = splitLines(before, halves.value);
    if (!before_lines) { return CountResult::failure(before_lines.code); }
    const Result<Lines, KiottySpecStatus> after_lines = splitLines(after, rest.value);
    if (!after_lines) { return CountResult::failure(after_lines.code); }
    const LinePair pair {before_lines.value, after_lines.value};
    const KiottySpecStatus checked = checkWork(work, pair.before.count, pair.after.count);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(checked); }
    fillMoves(pair, work);
    return walkMoves(pair, work.moves, out);
}

namespace {

Result<Lines, KiottySpecStatus> splitLines(Span<const char> text, Span<uint16_t> starts)
{
    using LinesResult = Result<Lines, KiottySpecStatus>;
    const size_t text_length = text.length();
    if (text_length > MAX_SEGMENT_OFFSET) {
        return LinesResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
    }
    Lines lines {text, starts, 0};
    starts[0] = 0;
    for (size_t index = 0; index < text_length; ++index) {
        const bool is_line_end = text[index] == NEW_LINE || index + 1 == text_length;
        if (!is_line_end) { continue; }
        if (lines.count + 1 >= starts.length()) {
            return LinesResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
        }
        ++lines.count;
        starts[lines.count] = static_cast<uint16_t>(index + 1);
    }
    return LinesResult::success(lines);
}

KiottySpecStatus checkWork(const LineWork& work, size_t before_count, size_t after_count)
{
    const bool has_moves = work.moves.length() >= before_count * after_count;
    const bool has_lengths = work.lengths.length() >= (after_count + 1) * 2;
    const bool is_enough = has_moves && has_lengths;
    return is_enough ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
}

void fillMoves(LinePair pair, const LineWork& work)
{
    const size_t row_cells = pair.after.count + 1;
    for (size_t column = 0; column < row_cells; ++column) { work.lengths[column] = 0; }
    for (size_t row = pair.before.count; row > 0; --row) {
        fillRow(pair, row - 1, work.lengths, work.moves);
        for (size_t column = 0; column < row_cells; ++column) {
            work.lengths[column] = work.lengths[row_cells + column];
        }
    }
}

void fillRow(LinePair pair, size_t row, Span<uint16_t> lengths, Span<uint8_t> moves)
{
    const size_t columns = pair.after.count;
    const size_t current = columns + 1;
    lengths[current + columns] = 0;
    for (size_t column = columns; column > 0; --column) {
        const size_t at = column - 1;
        const size_t move = row * columns + at;
        const bool is_same = isSameLine(pair, row, at);
        const uint16_t down = lengths[at];
        const uint16_t right = lengths[current + at + 1];
        const LineMove chosen = is_same ? LineMove::SAME : largerMove(down, right);
        const uint16_t diagonal = static_cast<uint16_t>(lengths[at + 1] + 1);
        const uint16_t best = down >= right ? down : right;
        lengths[current + at] = is_same ? diagonal : best;
        moves[move] = static_cast<uint8_t>(chosen);
    }
}

LineMove largerMove(uint16_t down, uint16_t right)
{
    return down >= right ? LineMove::REMOVE : LineMove::ADD;
}

bool isSameLine(LinePair pair, size_t before, size_t after)
{
    const Span<const char> left = lineAt(pair.before, before);
    const Span<const char> right = lineAt(pair.after, after);
    const size_t length = left.length();
    if (length != right.length()) { return false; }
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) { return false; }
    }
    return true;
}

Span<const char> lineAt(const Lines& lines, size_t index)
{
    const size_t start = lines.starts[index];
    const size_t end = lines.starts[index + 1];
    const auto line = lines.text.subspan(start, end - start);
    return line ? line.value : Span<const char>();
}

CountResult walkMoves(LinePair pair, Span<const uint8_t> moves, Span<TextSegment> out)
{
    SegmentList list {out, 0};
    Position position {};
    while (position.before < pair.before.count || position.after < pair.after.count) {
        const LineMove move = readMove(pair, moves, position);
        const bool is_after = move == LineMove::ADD;
        const Lines& lines = is_after ? pair.after : pair.before;
        const size_t index = is_after ? position.after : position.before;
        const TextChange change = changeOf(move);
        const KiottySpecStatus appended = appendLine(list, lines, index, change);
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(appended); }
        position.before += move == LineMove::ADD ? 0 : 1;
        position.after += move == LineMove::REMOVE ? 0 : 1;
    }
    return CountResult::success(list.count);
}

LineMove readMove(LinePair pair, Span<const uint8_t> moves, Position position)
{
    if (position.before == pair.before.count) { return LineMove::ADD; }
    if (position.after == pair.after.count) { return LineMove::REMOVE; }
    const size_t cell = position.before * pair.after.count + position.after;
    return static_cast<LineMove>(moves[cell]);
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

KiottySpecStatus appendLine(SegmentList& list, const Lines& lines, size_t index, TextChange change)
{
    const uint16_t start = lines.starts[index];
    const uint16_t length = static_cast<uint16_t>(lines.starts[index + 1] - start);
    const TextSource source = change == TextChange::ADDED ? TextSource::AFTER : TextSource::BEFORE;
    if (list.count > 0) {
        TextSegment& last = list.out[list.count - 1];
        const bool is_joined = last.change == change && last.offset + last.length == start;
        if (is_joined) {
            last.length = static_cast<uint16_t>(last.length + length);
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
    }
    if (list.count >= list.out.length()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    list.out[list.count] = TextSegment {start, length, 0, change, source};
    ++list.count;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

}
