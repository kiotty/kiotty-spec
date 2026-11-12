#include "implementation/context/kiotty_differ.h"

#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_version_table.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr uint32_t ITEM_AFTER_SHIFT = 32;
constexpr uint32_t ITEM_BEFORE_MASK = 0xFFFFFFFFu;
constexpr uint32_t ITEM_KIND_MASK = 0xFFu;
constexpr uint32_t ITEM_CHANGE_SHIFT = 8;
constexpr uint32_t ITEM_CHANGE_MASK = 0xFFu;
constexpr uint32_t ITEM_FIELDS_SHIFT = 16;
constexpr uint32_t ITEM_FIELDS_MASK = 0xFFFFu;
constexpr uint32_t RELATION_CHECKPOINT_MASK = 0xFFFFFFFFu;
constexpr uint32_t RELATION_ID_SHIFT = 32;
constexpr uint32_t RELATION_ID_MASK = 0xFFFFu;
constexpr uint32_t RELATION_MODEL_SHIFT = 48;
constexpr uint32_t RELATION_MODEL_MASK = 0xFFu;
constexpr uint32_t RELATION_CHANGE_SHIFT = 56;
constexpr uint32_t CHECKPOINT_SEQUENCE_MASK = 0xFFFFFFFFu;

constexpr uint32_t MAX_KIND_BIT = 32;

bool isKindInMask(uint32_t mask, uint32_t kind);
}

ItemDiffRow unpackItemDiff(const CursorRow& row)
{
    return ItemDiffRow { unpackId(row.first, row.second),
                         static_cast<uint32_t>(row.third & ITEM_BEFORE_MASK),
                         static_cast<uint32_t>(row.third >> ITEM_AFTER_SHIFT),
                         static_cast<uint32_t>(row.fourth & ITEM_KIND_MASK),
                         static_cast<uint32_t>((row.fourth >> ITEM_CHANGE_SHIFT) &
                                               ITEM_CHANGE_MASK),
                         static_cast<uint32_t>((row.fourth >> ITEM_FIELDS_SHIFT) &
                                               ITEM_FIELDS_MASK) };
}

RelationDiffRow unpackRelationDiff(const CursorRow& row)
{
    const uint64_t number = static_cast<uint64_t>(row.number);
    return RelationDiffRow { unpackId(row.first, row.second),
                             unpackId(row.third, row.fourth),
                             static_cast<uint32_t>(number & RELATION_CHECKPOINT_MASK),
                             static_cast<uint32_t>((number >> RELATION_ID_SHIFT) &
                                                   RELATION_ID_MASK),
                             static_cast<uint32_t>((number >> RELATION_MODEL_SHIFT) &
                                                   RELATION_MODEL_MASK),
                             static_cast<uint32_t>(number >> RELATION_CHANGE_SHIFT) };
}

Result<DiffRange, KiottySpecStatus> Differ::range(uint64_t from, uint64_t to)
{
    using RangeResult = Result<DiffRange, KiottySpecStatus>;
    if (from == KIOTTY_SPEC_CHECKPOINT_CURRENT) {
        return RangeResult::failure(_workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                             KIOTTY_SPEC_SUBJECT_DIFF_RANGE));
    }
    const auto start = point(from);
    if (!start) { return RangeResult::failure(start.code); }
    const auto end = point(to);
    if (!end) { return RangeResult::failure(end.code); }
    if (start.value >= end.value) {
        return RangeResult::failure(_workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                             KIOTTY_SPEC_SUBJECT_DIFF_RANGE));
    }
    return RangeResult::success(DiffRange{start.value, end.value});
}

Result<uint32_t, KiottySpecStatus> Differ::point(uint64_t checkpoint)
{
    using PointResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = checkpoint == KIOTTY_SPEC_CHECKPOINT_CURRENT
                                  ? _workspace.checkpoints.latest()
                                  : static_cast<uint32_t>(checkpoint & CHECKPOINT_SEQUENCE_MASK);
    const auto checkpoint_record = _workspace.checkpoints.get(sequence);
    if (!checkpoint_record) {
        return PointResult::failure(
            _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    return PointResult::success(sequence);
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
    ItemDiffQuery query(_workspace.items, range, kind_mask, field_mask);
    const KiottySpecStatus opened = query.open();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    const Span<KindCounts> kind_counts(counts, entity::ITEM_KIND_COUNT);
    Result<bool, KiottySpecStatus> next = query.next();
    for (; next && next.value; next = query.next()) {
        const Result<KindCounts&, KiottySpecStatus> found =
            kind_counts.at(query.current().kind - 1);
        if (!found) { return found.code; }
        KindCounts& count = *found.value;
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
    RelationDiffQuery query(_workspace.items, _workspace.relations, range, kind_mask);
    const KiottySpecStatus opened = query.open();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    const Span<KindCounts> kind_counts(counts, entity::ITEM_KIND_COUNT);
    Result<bool, KiottySpecStatus> next = query.next();
    for (; next && next.value; next = query.next()) {
        const Result<KindCounts&, KiottySpecStatus> found =
            kind_counts.at(query.current().model - 1);
        if (!found) { return found.code; }
        KindCounts& count = *found.value;
        const uint32_t change = query.current().change;
        if (change == KIOTTY_SPEC_CHANGE_ADDED) {
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
    const Span<KindCounts> kind_counts(counts, entity::ITEM_KIND_COUNT);
    for (uint32_t kind = 1; kind <= entity::ITEM_KIND_COUNT; ++kind) {
        const Result<KindCounts&, KiottySpecStatus> found = kind_counts.at(kind - 1);
        if (!found) { return found.code; }
        KindCounts& count = *found.value;
        const VersionTable<entity::DecisionVersion>& versions = _workspace.decisions.versions();
        const DecisionVersionResult before = versions.latest(kind, range.from);
        const DecisionVersionResult after = versions.latest(kind, range.to);
        const bool has_changed = before.value != after.value;
        const bool is_in_mask = isKindInMask(kind_mask, kind);
        if (is_in_mask && has_changed) { count.has_decision_change = true; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

CursorIdResult Differ::items(DiffRange range, uint32_t kind_mask, uint32_t field_mask)
{
    const auto cursor = _cursors.open(CursorKind::ITEM_DIFF);
    if (!cursor) {
        return CursorIdResult::failure(makeCursorWriter().raiseCursorFull(cursor.code));
    }
    ItemDiffQuery query(_workspace.items, range, kind_mask, field_mask);
    KiottySpecStatus status = query.open();
    Result<bool, KiottySpecStatus> next = status == KIOTTY_SPEC_STATUS_SUCCESS
                                              ? query.next()
                                              : Result<bool, KiottySpecStatus>::failure(status);
    for (; next && next.value && status == KIOTTY_SPEC_STATUS_SUCCESS; next = query.next()) {
        const ItemDiffRow& diff = query.current();
        CursorRow row {};
        row.first = packIdWord(diff.id, 0);
        row.second = packIdWord(diff.id, PACKED_ID_WORD_BYTES);
        row.third = diff.before | (static_cast<uint64_t>(diff.after) << ITEM_AFTER_SHIFT);
        row.fourth = diff.kind | (static_cast<uint64_t>(diff.change) << ITEM_CHANGE_SHIFT) |
                     (static_cast<uint64_t>(diff.fields) << ITEM_FIELDS_SHIFT);
        status = makeCursorWriter().pushRow(cursor.value, row);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}

CursorIdResult Differ::relations(DiffRange range, uint32_t kind_mask)
{
    const auto cursor = _cursors.open(CursorKind::RELATION_DIFF);
    if (!cursor) {
        return CursorIdResult::failure(makeCursorWriter().raiseCursorFull(cursor.code));
    }
    RelationDiffQuery query(_workspace.items, _workspace.relations, range, kind_mask);
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
            diff.checkpoint |
            (static_cast<uint64_t>(diff.relation & RELATION_ID_MASK) << RELATION_ID_SHIFT) |
            (static_cast<uint64_t>(diff.model) << RELATION_MODEL_SHIFT) |
            (static_cast<uint64_t>(diff.change) << RELATION_CHANGE_SHIFT));
        status = makeCursorWriter().pushRow(cursor.value, row);
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}

namespace {

bool isKindInMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

}

}
