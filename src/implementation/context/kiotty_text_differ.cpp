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

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field);
KiottySpecStatus storeTexts(Span<char> area, Span<const char> before, Span<const char> after);
}

CursorIdResult TextDiffer::open(entity::ItemHandle item, uint32_t field, DiffRange range)
{
    const Result<IdText, KiottySpecStatus> id = readItemId(item);
    if (!id) { return CursorIdResult::failure(id.code); }
    const auto before = PastReader(_workspace).itemAt(id.value.view(), range.from);
    const auto after = PastReader(_workspace).itemAt(id.value.view(), range.to);
    const bool has_any_version = before || after;
    if (!has_any_version) {
        return CursorIdResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id.value.view()));
    }
    const TextPair texts {selectText(before, field), selectText(after, field)};
    const auto cursor = _cursors.open(CursorKind::TEXT_DIFF);
    CursorWriter writer = makeCursorWriter();
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const KiottySpecStatus filled = fillSegments(cursor.value, texts);
    if (filled != KIOTTY_SPEC_STATUS_SUCCESS) {
        return writer.closeWith(cursor.value, writer.raiseRowsFull(filled));
    }
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

Result<entity::IdText, KiottySpecStatus> TextDiffer::readItemId(entity::ItemHandle item)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    const auto record = _workspace.items.reader().get(item);
    if (!record) {
        return IdResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_HANDLE, KIOTTY_SPEC_SUBJECT_ITEM));
    }
    return IdResult::success(record.value->id);
}

KiottySpecStatus TextDiffer::fillSegments(CursorId cursor, const TextPair& texts)
{
    const auto space = _cursors.space(cursor, CursorKind::TEXT_DIFF);
    if (!space) { return space.code; }
    char* const bytes = reinterpret_cast<char*>(space.value.rows.data());
    const size_t room = space.value.rows.length() * sizeof(CursorRow);
    const size_t segment_room =
        room > SEGMENT_OFFSET ? (room - SEGMENT_OFFSET) / sizeof(TextSegment) : 0;
    if (segment_room == 0) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const KiottySpecStatus stored = storeTexts(Span<char>(bytes, room), texts.before, texts.after);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) { return stored; }
    Span<char> scratch = _work.scratch();
    const Span<TextSegment> segments(reinterpret_cast<TextSegment*>(bytes + SEGMENT_OFFSET),
                                     segment_room < MAX_SEGMENTS ? segment_room : MAX_SEGMENTS);
    const Result<size_t, KiottySpecStatus> count =
        diffText(Span<const char>(bytes, texts.before.length()),
                 Span<const char>(bytes + TEXT_SLOT_BYTES, texts.after.length()),
                 Span<uint8_t>(reinterpret_cast<uint8_t*>(scratch.data()), scratch.length()),
                 segments);
    if (!count) { return count.code; }
    space.value.slot->length = count.value;
    space.value.slot->position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field)
{
    if (!version) { return Span<const char>(); }
    const entity::ItemRecord& record = version.value.record;
    return field == KIOTTY_SPEC_TEXT_FIELD_TITLE ? record.title.view() : record.summary.view();
}

KiottySpecStatus storeTexts(Span<char> area, Span<const char> before, Span<const char> after)
{
    const Result<Span<char>, KiottySpecStatus> before_slot = area.subspan(0, TEXT_SLOT_BYTES);
    if (!before_slot) { return before_slot.code; }
    const Result<Span<char>, KiottySpecStatus> after_slot =
        area.subspan(TEXT_SLOT_BYTES, TEXT_SLOT_BYTES);
    if (!after_slot) { return after_slot.code; }
    const KiottySpecStatus before_copied = before_slot.value.copyFrom(before);
    if (before_copied != KIOTTY_SPEC_STATUS_SUCCESS) { return before_copied; }
    return after_slot.value.copyFrom(after);
}

}

}
