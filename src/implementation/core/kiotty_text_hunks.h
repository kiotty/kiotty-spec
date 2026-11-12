#pragma once

#include "kiotty_span.h"
#include "kiotty_text_diff.h"
#include "kiotty_utf8.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::text_hunk {

constexpr size_t MAX_POINTS = MAX_DIFF_TEXT_BYTES;
constexpr size_t MAX_HUNKS = MAX_POINTS * 2 + 1;

struct Points {
    uint16_t starts[MAX_POINTS + 1] {};
    size_t count {};
};

struct SplitText {
    Points points {};
    Span<const char> text {};
};

struct DiffTexts {
    const SplitText& before;
    const SplitText& after;
};

enum class HunkKind : uint8_t { EQUAL, EDIT };

struct Hunk {
    uint16_t before_start {};
    uint16_t before_end {};
    uint16_t after_start {};
    uint16_t after_end {};
    HunkKind kind {HunkKind::EQUAL};
    uint8_t reserved {};
};

struct HunkCursor {
    size_t index {};
    size_t count {};
};

static_assert(sizeof(Points) == 520, "unexpected padding");
static_assert(sizeof(SplitText) == 536, "unexpected padding");
static_assert(sizeof(DiffTexts) == 16, "unexpected padding");
static_assert(sizeof(Hunk) == 10, "unexpected padding");
static_assert(sizeof(HunkCursor) == 16, "unexpected padding");

inline size_t appendHunk(Span<Hunk> hunks, size_t count, Hunk hunk);
inline size_t mergeShortEqualities(Span<Hunk> hunks, size_t count);
inline size_t mergeFirstShortEquality(Span<Hunk> hunks, size_t count);
inline bool isShortEqualityAt(Span<const Hunk> hunks, size_t index);
inline size_t measureLargerSide(Hunk hunk);
inline size_t extendEditsToWords(Span<Hunk> hunks, size_t count, DiffTexts texts);
inline HunkCursor extendEditToWords(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline HunkCursor extendEditEnd(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline bool canMoveEqualForward(Span<const Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline bool isOpenAtEnd(Hunk edit, DiffTexts texts);
inline HunkCursor moveEqualForward(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline HunkCursor removeEmptyEqualAfter(Span<Hunk> hunks, HunkCursor cursor);
inline HunkCursor extendEditStart(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline bool canMoveEqualBackward(Span<const Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline bool isOpenAtStart(Hunk edit, DiffTexts texts);
inline HunkCursor moveEqualBackward(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts);
inline HunkCursor mergeWithPreviousEdit(Span<Hunk> hunks, HunkCursor cursor);
inline size_t eraseHunk(Span<Hunk> hunks, size_t count, size_t index);
inline bool isSpace(char letter);
inline size_t findNextPointEnd(Span<const char> text, size_t start);
inline size_t findPreviousPointStart(Span<const char> text, size_t end);

class HunkList
{
public:
    void add(Hunk hunk) { _count = appendHunk(viewHunks(), _count, hunk); }
    void mergeEqualities() { _count = mergeShortEqualities(viewHunks(), _count); }
    void extendToWords(DiffTexts texts) { _count = extendEditsToWords(viewHunks(), _count, texts); }
    Span<const Hunk> view() const { return Span<const Hunk>(_hunks, _count); }

private:
    Span<Hunk> viewHunks() { return Span<Hunk>(_hunks, MAX_HUNKS); }

    Hunk _hunks[MAX_HUNKS] {};
    size_t _count {};
};

inline size_t appendHunk(Span<Hunk> hunks, size_t count, Hunk hunk)
{
    const bool is_continued = count > 0 && hunks[count - 1].kind == hunk.kind;
    if (is_continued) {
        hunks[count - 1].before_end = hunk.before_end;
        hunks[count - 1].after_end = hunk.after_end;
        return count;
    }
    hunks[count] = hunk;
    return count + 1;
}

inline size_t mergeShortEqualities(Span<Hunk> hunks, size_t count)
{
    size_t previous = count;
    size_t merged = mergeFirstShortEquality(hunks, count);
    while (merged != previous) {
        previous = merged;
        merged = mergeFirstShortEquality(hunks, merged);
    }
    return merged;
}

inline size_t mergeFirstShortEquality(Span<Hunk> hunks, size_t count)
{
    for (size_t index = 1; index + 1 < count; ++index) {
        const bool is_short = isShortEqualityAt(hunks, index);
        if (is_short) {
            const Hunk left = hunks[index - 1];
            const Hunk right = hunks[index + 1];
            hunks[index - 1] = Hunk {left.before_start, right.before_end, left.after_start,
                                     right.after_end, HunkKind::EDIT, 0};
            const size_t without_equal = eraseHunk(hunks, count, index);
            return eraseHunk(hunks, without_equal, index);
        }
    }
    return count;
}

inline bool isShortEqualityAt(Span<const Hunk> hunks, size_t index)
{
    const Hunk equal = hunks[index];
    const Hunk left = hunks[index - 1];
    const Hunk right = hunks[index + 1];
    const bool is_between_edits = equal.kind == HunkKind::EQUAL && left.kind == HunkKind::EDIT &&
                                  right.kind == HunkKind::EDIT;
    if (!is_between_edits) { return false; }
    const size_t length = static_cast<size_t>(equal.before_end) - equal.before_start;
    const size_t left_side = measureLargerSide(left);
    const size_t right_side = measureLargerSide(right);
    return length <= left_side && length <= right_side;
}

inline size_t measureLargerSide(Hunk hunk)
{
    const size_t before = static_cast<size_t>(hunk.before_end) - hunk.before_start;
    const size_t after = static_cast<size_t>(hunk.after_end) - hunk.after_start;
    return before > after ? before : after;
}

inline size_t extendEditsToWords(Span<Hunk> hunks, size_t count, DiffTexts texts)
{
    HunkCursor cursor {0, count};
    while (cursor.index < cursor.count) {
        cursor = extendEditToWords(hunks, cursor, texts);
        ++cursor.index;
    }
    return cursor.count;
}

inline HunkCursor extendEditToWords(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    const bool is_edit = hunks[cursor.index].kind == HunkKind::EDIT;
    if (!is_edit) { return cursor; }
    const HunkCursor extended = extendEditEnd(hunks, cursor, texts);
    return extendEditStart(hunks, extended, texts);
}

inline HunkCursor extendEditEnd(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    HunkCursor moved = cursor;
    bool can_move = canMoveEqualForward(hunks, moved, texts);
    while (can_move) {
        moved = moveEqualForward(hunks, moved, texts);
        can_move = canMoveEqualForward(hunks, moved, texts);
    }
    return moved;
}

inline bool canMoveEqualForward(Span<const Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    const size_t next = cursor.index + 1;
    const bool has_next_equal = next < cursor.count && hunks[next].kind == HunkKind::EQUAL;
    if (!has_next_equal) { return false; }
    const bool is_open = isOpenAtEnd(hunks[cursor.index], texts);
    if (!is_open) { return false; }
    const bool is_space_after = isSpace(texts.after.text[hunks[next].after_start]);
    return !is_space_after;
}

inline bool isOpenAtEnd(Hunk edit, DiffTexts texts)
{
    const size_t before_last = static_cast<size_t>(edit.before_end) - 1;
    const size_t after_last = static_cast<size_t>(edit.after_end) - 1;
    const bool is_before_open =
        edit.before_end > edit.before_start && !isSpace(texts.before.text[before_last]);
    const bool is_after_open =
        edit.after_end > edit.after_start && !isSpace(texts.after.text[after_last]);
    return is_before_open || is_after_open;
}

inline HunkCursor moveEqualForward(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    Hunk& edit = hunks[cursor.index];
    Hunk& equal = hunks[cursor.index + 1];
    const size_t width = findNextPointEnd(texts.after.text, equal.after_start) - equal.after_start;
    edit.before_end = static_cast<uint16_t>(edit.before_end + width);
    edit.after_end = static_cast<uint16_t>(edit.after_end + width);
    equal.before_start = static_cast<uint16_t>(equal.before_start + width);
    equal.after_start = static_cast<uint16_t>(equal.after_start + width);
    return removeEmptyEqualAfter(hunks, cursor);
}

inline HunkCursor removeEmptyEqualAfter(Span<Hunk> hunks, HunkCursor cursor)
{
    const size_t next = cursor.index + 1;
    const bool is_empty = hunks[next].after_start == hunks[next].after_end;
    if (!is_empty) { return cursor; }
    const size_t count = eraseHunk(hunks, cursor.count, next);
    const bool has_next_edit = next < count && hunks[next].kind == HunkKind::EDIT;
    if (!has_next_edit) { return HunkCursor {cursor.index, count}; }
    hunks[cursor.index].before_end = hunks[next].before_end;
    hunks[cursor.index].after_end = hunks[next].after_end;
    return HunkCursor {cursor.index, eraseHunk(hunks, count, next)};
}

inline HunkCursor extendEditStart(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    HunkCursor moved = cursor;
    bool can_move = canMoveEqualBackward(hunks, moved, texts);
    while (can_move) {
        moved = moveEqualBackward(hunks, moved, texts);
        can_move = canMoveEqualBackward(hunks, moved, texts);
    }
    return moved;
}

inline bool canMoveEqualBackward(Span<const Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    const bool has_previous_equal =
        cursor.index > 0 && hunks[cursor.index - 1].kind == HunkKind::EQUAL;
    if (!has_previous_equal) { return false; }
    const bool is_open = isOpenAtStart(hunks[cursor.index], texts);
    if (!is_open) { return false; }
    const size_t last = static_cast<size_t>(hunks[cursor.index - 1].after_end) - 1;
    const bool is_space_before = isSpace(texts.after.text[last]);
    return !is_space_before;
}

inline bool isOpenAtStart(Hunk edit, DiffTexts texts)
{
    const bool is_before_open =
        edit.before_end > edit.before_start && !isSpace(texts.before.text[edit.before_start]);
    const bool is_after_open =
        edit.after_end > edit.after_start && !isSpace(texts.after.text[edit.after_start]);
    return is_before_open || is_after_open;
}

inline HunkCursor moveEqualBackward(Span<Hunk> hunks, HunkCursor cursor, DiffTexts texts)
{
    Hunk& edit = hunks[cursor.index];
    Hunk& equal = hunks[cursor.index - 1];
    const size_t start = findPreviousPointStart(texts.after.text, equal.after_end);
    const size_t width = equal.after_end - start;
    edit.before_start = static_cast<uint16_t>(edit.before_start - width);
    edit.after_start = static_cast<uint16_t>(edit.after_start - width);
    equal.before_end = static_cast<uint16_t>(equal.before_end - width);
    equal.after_end = static_cast<uint16_t>(equal.after_end - width);
    const bool is_empty = equal.after_start == equal.after_end;
    if (!is_empty) { return cursor; }
    const size_t count = eraseHunk(hunks, cursor.count, cursor.index - 1);
    return mergeWithPreviousEdit(hunks, HunkCursor {cursor.index - 1, count});
}

inline HunkCursor mergeWithPreviousEdit(Span<Hunk> hunks, HunkCursor cursor)
{
    const size_t index = cursor.index;
    const bool has_previous_edit = index > 0 && hunks[index - 1].kind == HunkKind::EDIT;
    if (!has_previous_edit) { return cursor; }
    hunks[index - 1].before_end = hunks[index].before_end;
    hunks[index - 1].after_end = hunks[index].after_end;
    return HunkCursor {index - 1, eraseHunk(hunks, cursor.count, index)};
}

inline size_t eraseHunk(Span<Hunk> hunks, size_t count, size_t index)
{
    for (size_t next = index + 1; next < count; ++next) { hunks[next - 1] = hunks[next]; }
    return count - 1;
}

inline bool isSpace(char letter)
{ return letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r'; }

inline size_t findNextPointEnd(Span<const char> text, size_t start)
{
    size_t end = start + 1;
    const size_t text_length = text.length();
    while (end < text_length) {
        const bool is_continuation = isUtf8Continuation(text[end]);
        if (!is_continuation) { break; }
        ++end;
    }
    return end;
}

inline size_t findPreviousPointStart(Span<const char> text, size_t end)
{
    size_t start = end - 1;
    while (start > 0) {
        const bool is_continuation = isUtf8Continuation(text[start]);
        if (!is_continuation) { break; }
        --start;
    }
    return start;
}

}
