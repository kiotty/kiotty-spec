#include "implementation/context/kiotty_text_differ.h"

#include "implementation/context/kiotty_past_reader.h"
#include "implementation/core/kiotty_text_diff.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field);
CursorRow packSegment(TextSegment segment);
TextSegment unpackSegment(const CursorRow& row);
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
    const auto located = _cursors.locate(cursor, CursorKind::TEXT_DIFF);
    if (!located) { return PieceResult::failure(located.code); }
    const CursorRowsResult rows = _cursors.fetch(cursor, CursorKind::TEXT_DIFF, 1);
    if (!rows) { return PieceResult::failure(rows.code); }
    const Result<const CursorRow&, KiottySpecStatus> row = rows.value.at(0);
    if (!row) { return PieceResult::success(TextPiece{}); }
    const TextSegment segment = unpackSegment(*row.value);
    const auto text = _store.textOf(located.value.index, segment.source);
    if (!text) { return PieceResult::failure(text.code); }
    const auto piece = text.value.subspan(segment.offset, segment.length);
    if (!piece) { return PieceResult::failure(piece.code); }
    return PieceResult::success(TextPiece {piece.value, static_cast<uint32_t>(segment.change)});
}

Result<entity::IdText, KiottySpecStatus> TextDiffer::readItemId(entity::ItemHandle item)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    const auto label = _workspace.items.reader().label(item);
    if (!label) {
        return IdResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_HANDLE, KIOTTY_SPEC_SUBJECT_ITEM));
    }
    return IdResult::success((*label.value).id);
}

KiottySpecStatus TextDiffer::fillSegments(CursorId cursor, const TextPair& texts)
{
    const auto located = _cursors.locate(cursor, CursorKind::TEXT_DIFF);
    if (!located) { return located.code; }
    const KiottySpecStatus stored = storeTexts(located.value.index, texts);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) { return stored; }
    const Span<TextSegment> segments = _store.segments();
    const Result<size_t, KiottySpecStatus> count =
        diffText(texts.before, texts.after, _store.table(), segments);
    if (!count) { return count.code; }
    const auto written = segments.subspan(0, count.value);
    if (!written) { return written.code; }
    return pushSegments(cursor, written.value);
}

KiottySpecStatus TextDiffer::storeTexts(size_t cursor_index, const TextPair& texts)
{
    const auto before_slot = _store.textOf(cursor_index, TextSource::BEFORE);
    if (!before_slot) { return before_slot.code; }
    const auto after_slot = _store.textOf(cursor_index, TextSource::AFTER);
    if (!after_slot) { return after_slot.code; }
    const KiottySpecStatus before_copied = before_slot.value.copyFrom(texts.before);
    if (before_copied != KIOTTY_SPEC_STATUS_SUCCESS) { return before_copied; }
    return after_slot.value.copyFrom(texts.after);
}

KiottySpecStatus TextDiffer::pushSegments(CursorId cursor, Span<const TextSegment> segments)
{
    for (const TextSegment segment : segments) {
        const KiottySpecStatus pushed = _cursors.push(cursor, packSegment(segment));
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

Span<const char> selectText(const Result<ItemVersionView, KiottySpecStatus>& version,
                            uint32_t field)
{
    if (!version) { return Span<const char>(); }
    const entity::ItemLabel& label = version.value.label;
    return field == KIOTTY_SPEC_TEXT_FIELD_TITLE ? label.title.view() : label.summary.view();
}

CursorRow packSegment(TextSegment segment)
{
    CursorRow row {};
    row.first = segment.offset;
    row.second = segment.length;
    row.third = static_cast<uint64_t>(segment.change);
    row.fourth = static_cast<uint64_t>(segment.source);
    return row;
}

TextSegment unpackSegment(const CursorRow& row)
{
    TextSegment segment {};
    segment.offset = static_cast<uint16_t>(row.first);
    segment.length = static_cast<uint16_t>(row.second);
    segment.change = static_cast<TextChange>(row.third);
    segment.source = static_cast<TextSource>(row.fourth);
    return segment;
}

}

}
