#include "kiotty_text_diff.h"

#include "kiotty_checked.h"
#include "kiotty_text_hunks.h"
#include "kiotty_utf8.h"

namespace kiotty::spec::implementation {

namespace {

using text_hunk::DiffTexts;
using text_hunk::Hunk;
using text_hunk::HunkKind;
using text_hunk::HunkList;
using text_hunk::Points;
using text_hunk::SplitText;

struct CommonTable {
    Span<uint8_t> cells {};
    size_t width {};
};

static_assert(sizeof(CommonTable) == 24, "unexpected padding");

enum class TraceStep : uint8_t { BOTH, BEFORE, AFTER };

using CountResult = Result<size_t, KiottySpecStatus>;
using TableResult = Result<CommonTable, KiottySpecStatus>;

bool isTooLong(Span<const char> text);
Points splitPoints(Span<const char> text);
TableResult viewCommonTable(Span<uint8_t> table, DiffTexts texts);
void fillCommonLengths(DiffTexts texts, const CommonTable& table);
uint8_t measureCommonLength(DiffTexts texts, const CommonTable& table, size_t row, size_t column);
HunkList traceHunks(DiffTexts texts, const CommonTable& table);
TraceStep chooseTraceStep(DiffTexts texts, const CommonTable& table, size_t row, size_t column);
Hunk makeHunk(DiffTexts texts, TraceStep step, size_t row, size_t column);
bool isSamePoint(DiffTexts texts, size_t before_index, size_t after_index);
uint8_t readCell(const CommonTable& table, size_t row, size_t column);
size_t locateCell(const CommonTable& table, size_t row, size_t column);
CountResult writeSegments(Span<const Hunk> hunks, Span<TextSegment> out);
CountResult writeHunk(Hunk hunk, Span<TextSegment> out, size_t count);
TextSegment makeSegment(TextChange change, TextSource source, uint16_t start, uint16_t end);
CountResult writeSegment(Span<TextSegment> out, size_t count, TextSegment segment);

}

Result<size_t, KiottySpecStatus> diffText(Span<const char> before, Span<const char> after,
                                          Span<uint8_t> table, Span<TextSegment> out)
{
    const bool is_too_long = isTooLong(before) || isTooLong(after);
    if (is_too_long) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const SplitText before_text {splitPoints(before), before};
    const SplitText after_text {splitPoints(after), after};
    const DiffTexts texts {before_text, after_text};
    const TableResult common = viewCommonTable(table, texts);
    if (!common) { return CountResult::failure(common.code); }
    fillCommonLengths(texts, common.value);
    HunkList hunks = traceHunks(texts, common.value);
    hunks.mergeEqualities();
    hunks.extendToWords(texts);
    return writeSegments(hunks.view(), out);
}

namespace {

bool isTooLong(Span<const char> text) { return text.length() > MAX_DIFF_TEXT_BYTES; }

Points splitPoints(Span<const char> text)
{
    Points points {};
    size_t index = 0;
    const size_t text_length = text.length();
    while (index < text_length) {
        const size_t sequence_length = utf8SequenceLength(static_cast<unsigned char>(text[index]));
        const bool is_broken = sequence_length == 0 || index + sequence_length > text_length;
        points.starts[points.count] = static_cast<uint16_t>(index);
        ++points.count;
        index += is_broken ? 1 : sequence_length;
    }
    points.starts[points.count] = static_cast<uint16_t>(text_length);
    return points;
}

TableResult viewCommonTable(Span<uint8_t> table, DiffTexts texts)
{
    const size_t width = texts.after.points.count + 1;
    const Result<size_t, KiottySpecStatus> needed =
        checkedMultiply(texts.before.points.count + 1, width);
    if (!needed) { return TableResult::failure(needed.code); }
    const size_t table_length = table.length();
    if (table_length < needed.value) {
        return TableResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
    }
    return TableResult::success(CommonTable {table, width});
}

void fillCommonLengths(DiffTexts texts, const CommonTable& table)
{
    const size_t before_count = texts.before.points.count;
    const size_t after_count = texts.after.points.count;
    for (size_t row_end = before_count + 1; row_end > 0; --row_end) {
        for (size_t column_end = after_count + 1; column_end > 0; --column_end) {
            const size_t row = row_end - 1;
            const size_t column = column_end - 1;
            const uint8_t length = measureCommonLength(texts, table, row, column);
            table.cells[locateCell(table, row, column)] = length;
        }
    }
}

uint8_t measureCommonLength(DiffTexts texts, const CommonTable& table, size_t row, size_t column)
{
    const bool is_inside = row < texts.before.points.count && column < texts.after.points.count;
    if (!is_inside) { return 0; }
    const bool is_same = isSamePoint(texts, row, column);
    if (is_same) { return static_cast<uint8_t>(readCell(table, row + 1, column + 1) + 1); }
    const uint8_t down = readCell(table, row + 1, column);
    const uint8_t right = readCell(table, row, column + 1);
    return down > right ? down : right;
}

HunkList traceHunks(DiffTexts texts, const CommonTable& table)
{
    HunkList hunks {};
    size_t row = 0;
    size_t column = 0;
    const size_t before_count = texts.before.points.count;
    const size_t after_count = texts.after.points.count;
    while (row < before_count || column < after_count) {
        const TraceStep step = chooseTraceStep(texts, table, row, column);
        hunks.add(makeHunk(texts, step, row, column));
        row += step == TraceStep::AFTER ? 0 : 1;
        column += step == TraceStep::BEFORE ? 0 : 1;
    }
    return hunks;
}

TraceStep chooseTraceStep(DiffTexts texts, const CommonTable& table, size_t row, size_t column)
{
    const size_t before_count = texts.before.points.count;
    const size_t after_count = texts.after.points.count;
    const bool has_both_points = row < before_count && column < after_count;
    const bool is_same = has_both_points && isSamePoint(texts, row, column);
    if (is_same) { return TraceStep::BOTH; }
    if (column == after_count) { return TraceStep::BEFORE; }
    if (row == before_count) { return TraceStep::AFTER; }
    const uint8_t down = readCell(table, row + 1, column);
    const uint8_t right = readCell(table, row, column + 1);
    return down >= right ? TraceStep::BEFORE : TraceStep::AFTER;
}

Hunk makeHunk(DiffTexts texts, TraceStep step, size_t row, size_t column)
{
    const Points& before = texts.before.points;
    const Points& after = texts.after.points;
    const size_t before_end = step == TraceStep::AFTER ? row : row + 1;
    const size_t after_end = step == TraceStep::BEFORE ? column : column + 1;
    const HunkKind kind = step == TraceStep::BOTH ? HunkKind::EQUAL : HunkKind::EDIT;
    return Hunk {before.starts[row], before.starts[before_end], after.starts[column],
                 after.starts[after_end], kind, 0};
}

bool isSamePoint(DiffTexts texts, size_t before_index, size_t after_index)
{
    const Points& before = texts.before.points;
    const Points& after = texts.after.points;
    const size_t before_start = before.starts[before_index];
    const size_t after_start = after.starts[after_index];
    const size_t left = static_cast<size_t>(before.starts[before_index + 1]) - before_start;
    const size_t right = static_cast<size_t>(after.starts[after_index + 1]) - after_start;
    if (left != right) { return false; }
    for (size_t offset = 0; offset < left; ++offset) {
        const bool is_different =
            texts.before.text[before_start + offset] != texts.after.text[after_start + offset];
        if (is_different) { return false; }
    }
    return true;
}

uint8_t readCell(const CommonTable& table, size_t row, size_t column)
{ return table.cells[locateCell(table, row, column)]; }

size_t locateCell(const CommonTable& table, size_t row, size_t column)
{ return row * table.width + column; }

CountResult writeSegments(Span<const Hunk> hunks, Span<TextSegment> out)
{
    size_t count = 0;
    for (const Hunk& hunk : hunks) {
        const CountResult written = writeHunk(hunk, out, count);
        if (!written) { return written; }
        count = written.value;
    }
    return CountResult::success(count);
}

CountResult writeHunk(Hunk hunk, Span<TextSegment> out, size_t count)
{
    if (hunk.kind == HunkKind::EQUAL) {
        const TextSegment unchanged = makeSegment(TextChange::UNCHANGED, TextSource::AFTER,
                                                  hunk.after_start, hunk.after_end);
        return writeSegment(out, count, unchanged);
    }
    const TextSegment removed_segment = makeSegment(TextChange::REMOVED, TextSource::BEFORE,
                                                    hunk.before_start, hunk.before_end);
    const CountResult removed = writeSegment(out, count, removed_segment);
    if (!removed) { return removed; }
    const TextSegment added_segment =
        makeSegment(TextChange::ADDED, TextSource::AFTER, hunk.after_start, hunk.after_end);
    return writeSegment(out, removed.value, added_segment);
}

TextSegment makeSegment(TextChange change, TextSource source, uint16_t start, uint16_t end)
{ return TextSegment {start, static_cast<uint16_t>(end - start), 0, change, source}; }

CountResult writeSegment(Span<TextSegment> out, size_t count, TextSegment segment)
{
    if (segment.length == 0) { return CountResult::success(count); }
    const size_t out_length = out.length();
    if (count == out_length) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    out[count] = segment;
    return CountResult::success(count + 1);
}

}

}
