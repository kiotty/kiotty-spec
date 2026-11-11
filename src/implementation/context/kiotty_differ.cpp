#include "implementation/context/kiotty_differ.h"

#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr uint32_t SHIFT = 32;

constexpr uint32_t MAX_KIND_BIT = 32;

bool inMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }
}

ItemDiffRow unpackItemDiff(const CursorRow& row)
{
    return ItemDiffRow { unpackId(row.first, row.second),
                         static_cast<uint32_t>(row.third & 0xFFFFFFFFu),
                         static_cast<uint32_t>(row.third >> SHIFT),
                         static_cast<uint32_t>(row.fourth & 0xFFu),
                         static_cast<uint32_t>((row.fourth >> 8) & 0xFFu),
                         static_cast<uint32_t>((row.fourth >> 16) & 0xFFFFu) };
}

RelationDiffRow unpackRelationDiff(const CursorRow& row)
{
    const uint64_t number = static_cast<uint64_t>(row.number);
    return RelationDiffRow { unpackId(row.first, row.second),
                             unpackId(row.third, row.fourth),
                             static_cast<uint32_t>(number & 0xFFFFFFFFu),
                             static_cast<uint32_t>((number >> 32) & 0xFFFFu),
                             static_cast<uint32_t>((number >> 48) & 0xFFu),
                             static_cast<uint32_t>(number >> 56) };
}

Result<uint32_t, KiottySpecStatus> Differ::point(uint64_t checkpoint)
{
    using PointResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = checkpoint == KIOTTY_SPEC_CHECKPOINT_CURRENT
                                  ? _w.checkpoints.latest()
                                  : static_cast<uint32_t>(checkpoint & 0xFFFFFFFFu);
    if (_w.checkpoints.at(sequence) == nullptr) {
        return PointResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    return PointResult::success(sequence);
}

Result<DiffRange, KiottySpecStatus> Differ::range(uint64_t from, uint64_t to)
{
    using RangeResult = Result<DiffRange, KiottySpecStatus>;
    if (from == KIOTTY_SPEC_CHECKPOINT_CURRENT) {
        return RangeResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_DIFF_RANGE));
    }
    const auto start = point(from);
    if (!start) { return RangeResult::failure(start.code); }
    const auto end = point(to);
    if (!end) { return RangeResult::failure(end.code); }
    if (start.value >= end.value) {
        return RangeResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_DIFF_RANGE));
    }
    return RangeResult::success(DiffRange{start.value, end.value});
}

KiottySpecStatus Differ::summarize(DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                                   KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    for (KindCounts& count : counts) { count = KindCounts{}; }
    KiottySpecStatus status = countItems(range, kind_mask, field_mask, counts);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = countRelations(range, kind_mask, counts); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = countDecisions(range, kind_mask, counts); }
    return status;
}

KiottySpecStatus Differ::countItems(DiffRange range, uint32_t kind_mask, uint32_t field_mask,
                                    KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    ItemDiffQuery query(_w.items, range, kind_mask, field_mask);
    const KiottySpecStatus opened = query.open();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    Result<bool, KiottySpecStatus> next = query.next();
    for (; next && next.value; next = query.next()) {
        KindCounts& count = counts[query.current().kind - 1];
        const uint32_t change = query.current().change;
        if (change == KIOTTY_SPEC_CHANGE_ADDED) { ++count.added; }
        if (change == KIOTTY_SPEC_CHANGE_MODIFIED) { ++count.modified; }
        if (change == KIOTTY_SPEC_CHANGE_REMOVED) { ++count.removed; }
    }
    return next.code;
}

KiottySpecStatus Differ::countRelations(DiffRange range, uint32_t kind_mask,
                                        KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    RelationDiffQuery query(_w.items, _w.relations, range, kind_mask);
    const KiottySpecStatus opened = query.open();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    Result<bool, KiottySpecStatus> next = query.next();
    for (; next && next.value; next = query.next()) {
        KindCounts& count = counts[query.current().model - 1];
        if (query.current().change == KIOTTY_SPEC_CHANGE_ADDED) {
            ++count.relations_added;
        } else {
            ++count.relations_removed;
        }
    }
    return next.code;
}

KiottySpecStatus Differ::countDecisions(DiffRange range, uint32_t kind_mask,
                                        KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    for (uint32_t kind = 1; kind <= entity::ITEM_KIND_COUNT; ++kind) {
        const entity::DecisionVersion* before = _w.decisions.versions().latest(kind, range.from);
        const entity::DecisionVersion* after = _w.decisions.versions().latest(kind, range.to);
        const bool changed = before != after;
        if (inMask(kind_mask, kind) && changed) { counts[kind - 1].has_decision_change = true; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

CursorIdResult Differ::items(DiffRange range, uint32_t kind_mask, uint32_t field_mask)
{
    const auto cursor = _cursors.open(CursorKind::ITEM_DIFF);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    ItemDiffQuery query(_w.items, range, kind_mask, field_mask);
    KiottySpecStatus status = query.open();
    Result<bool, KiottySpecStatus> next = status == KIOTTY_SPEC_STATUS_SUCCESS
                                              ? query.next()
                                              : Result<bool, KiottySpecStatus>::failure(status);
    for (; next && next.value && status == KIOTTY_SPEC_STATUS_SUCCESS; next = query.next()) {
        const ItemDiffRow& diff = query.current();
        CursorRow row {};
        row.first = packIdWord(diff.id, 0);
        row.second = packIdWord(diff.id, PACKED_ID_WORD_BYTES);
        row.third = diff.before | (static_cast<uint64_t>(diff.after) << SHIFT);
        row.fourth = diff.kind | (static_cast<uint64_t>(diff.change) << 8) |
                     (static_cast<uint64_t>(diff.fields) << 16);
        status = pushRow(_cursors, _w.failure, cursor.value, row);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}

CursorIdResult Differ::relations(DiffRange range, uint32_t kind_mask)
{
    const auto cursor = _cursors.open(CursorKind::RELATION_DIFF);
    if (!cursor) { return CursorIdResult::failure(raiseCursorFull(_w.failure, cursor.code)); }
    RelationDiffQuery query(_w.items, _w.relations, range, kind_mask);
    KiottySpecStatus status = query.open();
    Result<bool, KiottySpecStatus> next = status == KIOTTY_SPEC_STATUS_SUCCESS
                                              ? query.next()
                                              : Result<bool, KiottySpecStatus>::failure(status);
    for (; next && next.value && status == KIOTTY_SPEC_STATUS_SUCCESS; next = query.next()) {
        const RelationDiffRow& diff = query.current();
        CursorRow row {};
        row.first = packIdWord(diff.from_id, 0);
        row.second = packIdWord(diff.from_id, PACKED_ID_WORD_BYTES);
        row.third = packIdWord(diff.to_id, 0);
        row.fourth = packIdWord(diff.to_id, PACKED_ID_WORD_BYTES);
        row.number = static_cast<int64_t>(
            diff.checkpoint | (static_cast<uint64_t>(diff.relation & 0xFFFFu) << 32) |
            (static_cast<uint64_t>(diff.model) << 48) | (static_cast<uint64_t>(diff.change) << 56));
        status = pushRow(_cursors, _w.failure, cursor.value, row);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}


}
