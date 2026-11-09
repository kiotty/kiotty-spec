#include "implementation/context/kiotty_text_differ.h"

#include "implementation/context/kiotty_past_reader.h"
#include "implementation/core/kiotty_text_diff.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstring>

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;

constexpr size_t TEXT_SLOT_BYTES = 256;
constexpr size_t SEGMENT_OFFSET = TEXT_SLOT_BYTES * 2;
constexpr size_t MAX_SEGMENTS = MAX_DIFF_TEXT_BYTES * 2 + 1;

}

CursorIdResult TextDiffer::open(entity::ItemHandle item, uint32_t field, DiffRange range)
{
    if (field != KIOTTY_SPEC_TEXT_FIELD_TITLE && field != KIOTTY_SPEC_TEXT_FIELD_SUMMARY) {
        return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("글자 diff 는 제목이나 요약 하나에만 쓴다")}));
    }
    const auto record = _w.items.reader().get(item);
    if (!record) { return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_HANDLE, {literalText("항목 핸들이 무효임")})); }
    const IdText id = record.value->id;
    const auto before = PastReader(_w).itemAt(id.view(), range.from);
    const auto after = PastReader(_w).itemAt(id.view(), range.to);
    if (!before && !after) { return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("두 시점 모두 없는 항목: "), id.view()})); }
    const bool title = field == KIOTTY_SPEC_TEXT_FIELD_TITLE;
    const Span<const char> before_text = !before ? Span<const char>() : title ? before.value.record.title.view() : before.value.record.summary.view();
    const Span<const char> after_text = !after ? Span<const char>() : title ? after.value.record.title.view() : after.value.record.summary.view();

    const auto cursor = _cursors.open(CursorKind::TEXT_DIFF);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    const auto space = _cursors.space(cursor.value, CursorKind::TEXT_DIFF);
    char* bytes = reinterpret_cast<char*>(space.value.rows.data());
    const size_t room = space.value.rows.length() * sizeof(CursorRow);
    const size_t segment_room = room > SEGMENT_OFFSET ? (room - SEGMENT_OFFSET) / sizeof(TextSegment) : 0;
    if (segment_room == 0) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("max_cursor_rows 가 글자 diff 에 모자람")}));
    }
    std::memcpy(bytes, before_text.data(), before_text.length());
    std::memcpy(bytes + TEXT_SLOT_BYTES, after_text.data(), after_text.length());
    Span<char> scratch = _work.scratch();
    const auto count = diffText(Span<const char>(bytes, before_text.length()), Span<const char>(bytes + TEXT_SLOT_BYTES, after_text.length()),
                                Span<uint8_t>(reinterpret_cast<uint8_t*>(scratch.data()), scratch.length()),
                                Span<TextSegment>(reinterpret_cast<TextSegment*>(bytes + SEGMENT_OFFSET), segment_room < MAX_SEGMENTS ? segment_room : MAX_SEGMENTS));
    if (!count) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(_w.failure.raise(count.code, {literalText("글자 diff 를 담을 공간이 모자람")}));
    }
    space.value.slot->length = count.value;
    space.value.slot->position = 0;
    return cursor;
}

Result<TextPiece, KiottySpecStatus> TextDiffer::next(CursorId cursor)
{
    using PieceResult = Result<TextPiece, KiottySpecStatus>;
    const auto space = _cursors.space(cursor, CursorKind::TEXT_DIFF);
    if (!space) { return PieceResult::failure(space.code); }
    CursorSlot& slot = *space.value.slot;
    if (slot.position >= slot.length) { return PieceResult::success(TextPiece{}); }
    const char* bytes = reinterpret_cast<const char*>(space.value.rows.data());
    TextSegment segment {};
    std::memcpy(&segment, bytes + SEGMENT_OFFSET + slot.position * sizeof(TextSegment), sizeof(TextSegment));
    ++slot.position;
    const char* base = bytes + (segment.source == TextSource::AFTER ? TEXT_SLOT_BYTES : 0);
    return PieceResult::success(TextPiece{base + segment.offset, segment.length, static_cast<uint32_t>(segment.change)});
}

}
