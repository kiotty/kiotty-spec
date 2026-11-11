#include "implementation/context/kiotty_text_differ.h"

#include "implementation/context/kiotty_cursor_rows.h"
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

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field);
}

CursorIdResult TextDiffer::open(entity::ItemHandle item, uint32_t field, DiffRange range)
{
    const auto record = _workspace.items.reader().get(item);
    if (!record) {
        return CursorIdResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_HANDLE, KIOTTY_SPEC_SUBJECT_ITEM));
    }
    const IdText id = record.value->id;
    const auto before = PastReader(_workspace).itemAt(id.view(), range.from);
    const auto after = PastReader(_workspace).itemAt(id.view(), range.to);
    const bool has_any_version = before || after;
    if (!has_any_version) {
        return CursorIdResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id.view()));
    }
    const Span<const char> before_text = selectText(before, field);
    const Span<const char> after_text = selectText(after, field);

    const auto cursor = _cursors.open(CursorKind::TEXT_DIFF);
    if (!cursor) {
        return CursorIdResult::failure(raiseCursorFull(_workspace.failure, cursor.code));
    }
    const auto space = _cursors.space(cursor.value, CursorKind::TEXT_DIFF);
    char* const bytes = reinterpret_cast<char*>(space.value.rows.data());
    const size_t room = space.value.rows.length() * sizeof(CursorRow);
    const size_t segment_room =
        room > SEGMENT_OFFSET ? (room - SEGMENT_OFFSET) / sizeof(TextSegment) : 0;
    if (segment_room == 0) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, KIOTTY_SPEC_SUBJECT_CURSOR_ROWS,
            literalText("max_cursor_rows")));
    }
    std::memcpy(bytes, before_text.data(), before_text.length());
    std::memcpy(bytes + TEXT_SLOT_BYTES, after_text.data(), after_text.length());
    Span<char> scratch = _work.scratch();
    const auto count =
        diffText(Span<const char>(bytes, before_text.length()),
                 Span<const char>(bytes + TEXT_SLOT_BYTES, after_text.length()),
                 Span<uint8_t>(reinterpret_cast<uint8_t*>(scratch.data()), scratch.length()),
                 Span<TextSegment>(reinterpret_cast<TextSegment*>(bytes + SEGMENT_OFFSET),
                                   segment_room < MAX_SEGMENTS ? segment_room : MAX_SEGMENTS));
    if (!count) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(_workspace.failure.raise(
            count.code, KIOTTY_SPEC_SUBJECT_CURSOR_ROWS, literalText("max_cursor_rows")));
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
    std::memcpy(&segment, bytes + SEGMENT_OFFSET + slot.position * sizeof(TextSegment),
                sizeof(TextSegment));
    ++slot.position;
    const char* base = bytes + (segment.source == TextSource::AFTER ? TEXT_SLOT_BYTES : 0);
    return PieceResult::success(TextPiece { Span<const char>(base + segment.offset, segment.length),
                                            static_cast<uint32_t>(segment.change) });
}

namespace {

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field)
{
    if (!version) { return Span<const char>(); }
    const entity::ItemRecord& record = version.value.record;
    return field == KIOTTY_SPEC_TEXT_FIELD_TITLE ? record.title.view() : record.summary.view();
}

}

}
