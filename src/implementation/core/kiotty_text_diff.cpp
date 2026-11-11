#include "kiotty_text_diff.h"

#include "kiotty_utf8.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_POINTS = MAX_DIFF_TEXT_BYTES;
constexpr size_t MAX_HUNKS = MAX_POINTS * 2 + 1;

// 글자마다 시작 바이트. starts[count] 는 끝 바이트
struct Points {
    uint16_t starts[MAX_POINTS + 1] {};
    size_t count {};
};

bool splitPoints(Span<const char> text, Points& points)
{
    points.count = 0;
    size_t index = 0;
    while (index < text.length()) {
        size_t length = utf8SequenceLength(static_cast<unsigned char>(text[index]));
        if (length == 0 || index + length > text.length()) { length = 1; }
        points.starts[points.count++] = static_cast<uint16_t>(index);
        index += length;
    }
    points.starts[points.count] = static_cast<uint16_t>(text.length());
    return true;
}

bool samePoint(Span<const char> before, const Points& a, size_t i, Span<const char> after, const Points& b, size_t j)
{
    const size_t left = static_cast<size_t>(a.starts[i + 1]) - a.starts[i];
    const size_t right = static_cast<size_t>(b.starts[j + 1]) - b.starts[j];
    if (left != right) { return false; }
    for (size_t offset = 0; offset < left; ++offset) {
        if (before[a.starts[i] + offset] != after[b.starts[j] + offset]) { return false; }
    }
    return true;
}

// 같은 구간이면 before 와 after 의 길이가 같다. 편집이면 둘 중 하나가 0 일 수 있다.
struct Hunk {
    bool edit {};
    uint16_t before_start {};
    uint16_t before_end {};
    uint16_t after_start {};
    uint16_t after_end {};
};

struct Hunks {
    Hunk items[MAX_HUNKS] {};
    size_t count {};

    void add(bool edit, size_t before_start, size_t before_end, size_t after_start, size_t after_end)
    {
        if (count > 0 && items[count - 1].edit == edit) {
            items[count - 1].before_end = static_cast<uint16_t>(before_end);
            items[count - 1].after_end = static_cast<uint16_t>(after_end);
            return;
        }
        items[count++] = Hunk{edit, static_cast<uint16_t>(before_start), static_cast<uint16_t>(before_end), static_cast<uint16_t>(after_start),
                              static_cast<uint16_t>(after_end)};
    }

    void erase(size_t index)
    {
        for (size_t next = index + 1; next < count; ++next) { items[next - 1] = items[next]; }
        --count;
    }
};

size_t largerSide(const Hunk& hunk)
{
    const size_t before = static_cast<size_t>(hunk.before_end) - hunk.before_start;
    const size_t after = static_cast<size_t>(hunk.after_end) - hunk.after_start;
    return before > after ? before : after;
}

// 양옆 편집보다 짧은 공통 구간을 편집에 합친다.
void mergeShortEqualities(Hunks& hunks)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t index = 1; index + 1 < hunks.count; ++index) {
            const Hunk& equal = hunks.items[index];
            const Hunk& left = hunks.items[index - 1];
            const Hunk& right = hunks.items[index + 1];
            if (equal.edit || !left.edit || !right.edit) { continue; }
            const size_t length = static_cast<size_t>(equal.before_end) - equal.before_start;
            if (length > largerSide(left) || length > largerSide(right)) { continue; }
            hunks.items[index - 1] = Hunk{true, left.before_start, right.before_end, left.after_start, right.after_end};
            hunks.erase(index);
            hunks.erase(index);
            changed = true;
            break;
        }
    }
}

bool isSpace(char letter) { return letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r'; }

size_t previousPointStart(Span<const char> text, size_t end)
{
    size_t start = end - 1;
    while (start > 0 && isUtf8Continuation(text[start])) { --start; }
    return start;
}

size_t nextPointEnd(Span<const char> text, size_t start)
{
    size_t end = start + 1;
    while (end < text.length() && isUtf8Continuation(text[end])) { ++end; }
    return end;
}

// 편집 한쪽 끝이 낱말 가운데이면 공통 구간의 글자를 편집으로 옮겨 공백 경계에 맞춘다.
void alignToWords(Hunks& hunks, Span<const char> before, Span<const char> after)
{
    for (size_t index = 0; index < hunks.count; ++index) {
        if (!hunks.items[index].edit) { continue; }
        // 오른쪽
        while (index + 1 < hunks.count && !hunks.items[index + 1].edit) {
            Hunk& edit = hunks.items[index];
            Hunk& equal = hunks.items[index + 1];
            const bool before_open = edit.before_end > edit.before_start && !isSpace(before[static_cast<size_t>(edit.before_end) - 1]);
            const bool after_open = edit.after_end > edit.after_start && !isSpace(after[static_cast<size_t>(edit.after_end) - 1]);
            if ((!before_open && !after_open) || isSpace(after[equal.after_start])) { break; }
            const size_t width = nextPointEnd(after, equal.after_start) - equal.after_start;
            edit.before_end = static_cast<uint16_t>(edit.before_end + width);
            edit.after_end = static_cast<uint16_t>(edit.after_end + width);
            equal.before_start = static_cast<uint16_t>(equal.before_start + width);
            equal.after_start = static_cast<uint16_t>(equal.after_start + width);
            if (equal.after_start == equal.after_end) {
                hunks.erase(index + 1);
                if (index + 1 < hunks.count && hunks.items[index + 1].edit) {
                    hunks.items[index].before_end = hunks.items[index + 1].before_end;
                    hunks.items[index].after_end = hunks.items[index + 1].after_end;
                    hunks.erase(index + 1);
                }
            }
        }
        // 왼쪽
        while (index > 0 && !hunks.items[index - 1].edit) {
            Hunk& edit = hunks.items[index];
            Hunk& equal = hunks.items[index - 1];
            const bool before_open = edit.before_end > edit.before_start && !isSpace(before[edit.before_start]);
            const bool after_open = edit.after_end > edit.after_start && !isSpace(after[edit.after_start]);
            if ((!before_open && !after_open) || isSpace(after[static_cast<size_t>(equal.after_end) - 1])) { break; }
            const size_t width = equal.after_end - previousPointStart(after, equal.after_end);
            edit.before_start = static_cast<uint16_t>(edit.before_start - width);
            edit.after_start = static_cast<uint16_t>(edit.after_start - width);
            equal.before_end = static_cast<uint16_t>(equal.before_end - width);
            equal.after_end = static_cast<uint16_t>(equal.after_end - width);
            if (equal.after_start == equal.after_end) {
                hunks.erase(index - 1);
                --index;
                if (index > 0 && hunks.items[index - 1].edit) {
                    hunks.items[index - 1].before_end = hunks.items[index].before_end;
                    hunks.items[index - 1].after_end = hunks.items[index].after_end;
                    hunks.erase(index);
                    --index;
                }
            }
        }
    }
}

}

Result<size_t, KiottySpecStatus> diffText(Span<const char> before, Span<const char> after, Span<uint8_t> table, Span<TextSegment> out)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    if (before.length() > MAX_DIFF_TEXT_BYTES || after.length() > MAX_DIFF_TEXT_BYTES) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    Points a {};
    Points b {};
    splitPoints(before, a);
    splitPoints(after, b);
    const size_t width = b.count + 1;
    if (table.length() < (a.count + 1) * width) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }

    // table[i * width + j] = before[i..] 와 after[j..] 의 LCS 길이
    for (size_t i = a.count + 1; i > 0; --i) {
        for (size_t j = b.count + 1; j > 0; --j) {
            const size_t row = i - 1;
            const size_t column = j - 1;
            uint8_t value = 0;
            if (row < a.count && column < b.count) {
                if (samePoint(before, a, row, after, b, column)) {
                    value = static_cast<uint8_t>(table[(row + 1) * width + column + 1] + 1);
                } else {
                    const uint8_t down = table[(row + 1) * width + column];
                    const uint8_t right = table[row * width + column + 1];
                    value = down > right ? down : right;
                }
            }
            table[row * width + column] = value;
        }
    }

    Hunks hunks {};
    size_t i = 0;
    size_t j = 0;
    while (i < a.count || j < b.count) {
        if (i < a.count && j < b.count && samePoint(before, a, i, after, b, j)) {
            hunks.add(false, a.starts[i], a.starts[i + 1], b.starts[j], b.starts[j + 1]);
            ++i;
            ++j;
        } else if (j == b.count || (i < a.count && table[(i + 1) * width + j] >= table[i * width + j + 1])) {
            hunks.add(true, a.starts[i], a.starts[i + 1], b.starts[j], b.starts[j]);
            ++i;
        } else {
            hunks.add(true, a.starts[i], a.starts[i], b.starts[j], b.starts[j + 1]);
            ++j;
        }
    }
    mergeShortEqualities(hunks);
    alignToWords(hunks, before, after);

    size_t count = 0;
    auto emit = [&](TextChange change, TextSource source, size_t start, size_t end) {
        if (end == start) { return true; }
        if (count == out.length()) { return false; }
        out[count++] = TextSegment{change, source, static_cast<uint16_t>(start), static_cast<uint16_t>(end - start), 0};
        return true;
    };
    for (size_t index = 0; index < hunks.count; ++index) {
        const Hunk& hunk = hunks.items[index];
        bool fits = true;
        if (hunk.edit) {
            fits = emit(TextChange::REMOVED, TextSource::BEFORE, hunk.before_start, hunk.before_end) &&
                   emit(TextChange::ADDED, TextSource::AFTER, hunk.after_start, hunk.after_end);
        } else {
            fits = emit(TextChange::UNCHANGED, TextSource::AFTER, hunk.after_start, hunk.after_end);
        }
        if (!fits) { return CountResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    }
    return CountResult::success(count);
}

}
