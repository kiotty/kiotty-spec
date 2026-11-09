#include "implementation/context/kiotty_differ.h"

#include "implementation/core/kiotty_text_view.h"

#include <cstring>

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemKind;

constexpr uint32_t SHIFT = 32;

constexpr char DECISION_DIFF_SQL[] =
    "SELECT k.kind, (SELECT MAX(checkpoint) FROM decision_version d WHERE d.kind = k.kind AND d.checkpoint <= ?1), "
    "(SELECT MAX(checkpoint) FROM decision_version d WHERE d.kind = k.kind AND d.checkpoint <= ?2) FROM (SELECT DISTINCT kind FROM decision_version) k";

constexpr uint32_t MAX_KIND_BIT = 32;

bool inMask(uint32_t mask, uint32_t kind) { return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

void packId(const IdText& id, uint64_t& first, uint64_t& second)
{
    char bytes[16] {};
    std::memcpy(bytes, id.c_str(), id.length());
    std::memcpy(&first, bytes, 8);
    std::memcpy(&second, bytes + 8, 8);
}

IdText unpackId(uint64_t first, uint64_t second)
{
    char bytes[17] {};
    std::memcpy(bytes, &first, 8);
    std::memcpy(bytes + 8, &second, 8);
    IdText id;
    id.assign(boundedText(bytes, 16));
    return id;
}

}

ItemDiffRow unpackItemDiff(const CursorRow& row)
{
    return ItemDiffRow{unpackId(row.first, row.second), static_cast<uint32_t>(row.third & 0xFFFFFFFFu), static_cast<uint32_t>(row.third >> SHIFT),
                       static_cast<uint32_t>(row.fourth & 0xFFu), static_cast<uint32_t>((row.fourth >> 8) & 0xFFu),
                       static_cast<uint32_t>((row.fourth >> 16) & 0xFFFFu)};
}

RelationDiffRow unpackRelationDiff(const CursorRow& row)
{
    const uint64_t number = static_cast<uint64_t>(row.number);
    return RelationDiffRow{unpackId(row.first, row.second), unpackId(row.third, row.fourth), static_cast<uint32_t>(number & 0xFFFFFFFFu),
                           static_cast<uint32_t>((number >> 32) & 0xFFFFu), static_cast<uint32_t>((number >> 48) & 0xFFu),
                           static_cast<uint32_t>(number >> 56)};
}

Result<uint32_t, KiottySpecStatus> Differ::point(uint64_t checkpoint)
{
    using PointResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = checkpoint == KIOTTY_SPEC_CHECKPOINT_CURRENT ? _w.checkpoints.latest() : static_cast<uint32_t>(checkpoint & 0xFFFFFFFFu);
    if (_w.checkpoints.at(sequence) == nullptr) {
        return PointResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 체크포인트 (정리로 지워졌거나 아직 없음)")}));
    }
    return PointResult::success(sequence);
}

Result<DiffRange, KiottySpecStatus> Differ::range(uint64_t from, uint64_t to)
{
    using RangeResult = Result<DiffRange, KiottySpecStatus>;
    if (from == KIOTTY_SPEC_CHECKPOINT_CURRENT) {
        return RangeResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("구간의 시작은 체크포인트여야 함")}));
    }
    const auto start = point(from);
    if (!start) { return RangeResult::failure(start.code); }
    const auto end = point(to);
    if (!end) { return RangeResult::failure(end.code); }
    if (start.value >= end.value) {
        return RangeResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("구간이 거꾸로이거나 비어 있음 (from < to)")}));
    }
    return RangeResult::success(DiffRange{start.value, end.value});
}

KiottySpecStatus Differ::summarize(DiffRange range, uint32_t kind_mask, uint32_t field_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    for (KindCounts& count : counts) { count = KindCounts{}; }
    KiottySpecStatus status = countItems(range, kind_mask, field_mask, counts);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = countRelations(range, kind_mask, counts); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = countDecisions(range, kind_mask, counts); }
    return status;
}

KiottySpecStatus Differ::countItems(DiffRange range, uint32_t kind_mask, uint32_t field_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    ItemDiffQuery query(_w.storage, range, kind_mask, field_mask);
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

KiottySpecStatus Differ::countRelations(DiffRange range, uint32_t kind_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    RelationDiffQuery query(_w.storage, range, kind_mask);
    const KiottySpecStatus opened = query.open();
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return opened; }
    Result<bool, KiottySpecStatus> next = query.next();
    for (; next && next.value; next = query.next()) {
        KindCounts& count = counts[query.current().model - 1];
        if (query.current().change == KIOTTY_SPEC_CHANGE_ADDED) { ++count.relations_added; } else { ++count.relations_removed; }
    }
    return next.code;
}

KiottySpecStatus Differ::countDecisions(DiffRange range, uint32_t kind_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    auto query = _w.storage.prepare(literalText(DECISION_DIFF_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, static_cast<int64_t>(range.from));
    query.value.bind(2, static_cast<int64_t>(range.to));
    Result<bool, KiottySpecStatus> stepped = query.value.step();
    for (; stepped && stepped.value; stepped = query.value.step()) {
        const Row row = query.value.row();
        const uint32_t kind = entity::parseKind(viewOf(row.text(0)));
        const bool changed = row.isNull(1) != row.isNull(2) || (!row.isNull(1) && row.integer(1) != row.integer(2));
        if (kind != 0 && inMask(kind_mask, kind) && changed) { counts[kind - 1].decision_changed = 1; }
    }
    return stepped ? KIOTTY_SPEC_STATUS_SUCCESS : _w.storage.check(stepped.code);
}

CursorIdResult Differ::items(DiffRange range, uint32_t kind_mask, uint32_t field_mask)
{
    const auto cursor = _cursors.open(CursorKind::ITEM_DIFF);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    ItemDiffQuery query(_w.storage, range, kind_mask, field_mask);
    KiottySpecStatus status = query.open();
    Result<bool, KiottySpecStatus> next = status == KIOTTY_SPEC_STATUS_SUCCESS ? query.next() : Result<bool, KiottySpecStatus>::failure(status);
    for (; next && next.value && status == KIOTTY_SPEC_STATUS_SUCCESS; next = query.next()) {
        const ItemDiffRow& diff = query.current();
        CursorRow row;
        packId(diff.id, row.first, row.second);
        row.third = diff.before | (static_cast<uint64_t>(diff.after) << SHIFT);
        row.fourth = diff.kind | (static_cast<uint64_t>(diff.change) << 8) | (static_cast<uint64_t>(diff.fields) << 16);
        status = pushRow(cursor.value, row, literalText("바뀐 항목이 max_cursor_rows 를 넘음. Kind 를 좁혀 다시 열 것"));
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}

CursorIdResult Differ::relations(DiffRange range, uint32_t kind_mask)
{
    const auto cursor = _cursors.open(CursorKind::RELATION_DIFF);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    RelationDiffQuery query(_w.storage, range, kind_mask);
    KiottySpecStatus status = query.open();
    Result<bool, KiottySpecStatus> next = status == KIOTTY_SPEC_STATUS_SUCCESS ? query.next() : Result<bool, KiottySpecStatus>::failure(status);
    for (; next && next.value && status == KIOTTY_SPEC_STATUS_SUCCESS; next = query.next()) {
        const RelationDiffRow& diff = query.current();
        CursorRow row;
        packId(diff.from_id, row.first, row.second);
        packId(diff.to_id, row.third, row.fourth);
        row.number = static_cast<int64_t>(diff.checkpoint | (static_cast<uint64_t>(diff.relation & 0xFFFFu) << 32) |
                                          (static_cast<uint64_t>(diff.model) << 48) | (static_cast<uint64_t>(diff.change) << 56));
        status = pushRow(cursor.value, row, literalText("바뀐 관계가 max_cursor_rows 를 넘음. Kind 를 좁혀 다시 열 것"));
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = next.code; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return cursor; }
    _cursors.close(cursor.value);
    return CursorIdResult::failure(status);
}

// 행이 넘치면 어떻게 좁힐지 남긴다.
KiottySpecStatus Differ::pushRow(CursorId cursor, const CursorRow& row, Span<const char> overflow)
{
    const KiottySpecStatus pushed = _cursors.push(cursor, row);
    if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) { return _w.failure.raise(pushed, {overflow}); }
    return pushed;
}

}
