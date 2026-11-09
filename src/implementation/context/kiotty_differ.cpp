#include "implementation/context/kiotty_differ.h"

#include "implementation/core/kiotty_text_diff.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstring>

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemKind;

constexpr uint32_t SHIFT = 32;
constexpr size_t TEXT_SLOT_BYTES = 256;
constexpr size_t SEGMENT_OFFSET = TEXT_SLOT_BYTES * 2;
constexpr size_t MAX_SEGMENTS = MAX_DIFF_TEXT_BYTES * 2 + 1;

#define KIND_ORDER(column)                                                                                                                      \
    "CASE " column " WHEN 'change_request' THEN 1 WHEN 'requirement' THEN 2 WHEN 'rule' THEN 3 WHEN 'external_interface' THEN 4 "            \
    "WHEN 'design' THEN 5 WHEN 'unit_test_plan' THEN 6 WHEN 'milestone' THEN 7 WHEN 'unit_test_result' THEN 8 "                              \
    "WHEN 'system_test_plan' THEN 9 WHEN 'system_test_result' THEN 10 WHEN 'code_inspection_result' THEN 11 "                               \
    "WHEN 'e2e_test_plan' THEN 12 WHEN 'e2e_test_result' THEN 13 ELSE 14 END"

#define ITEM_AT(name, parameter)                                                                                                                \
    name " AS (SELECT v.item_id, v.checkpoint, v.kind, v.parent_id, v.title, v.summary, v.body, v.tags, v.removed FROM item_version v "        \
         "JOIN (SELECT item_id, MAX(checkpoint) AS c FROM item_version WHERE checkpoint <= " parameter " GROUP BY item_id) m "                  \
         "ON v.item_id = m.item_id AND v.checkpoint = m.c)"

#define RELATION_AT(name, parameter)                                                                                                            \
    name " AS (SELECT v.from_id, v.model, v.relation, v.to_id, v.checkpoint, v.removed FROM relation_version v "                              \
         "JOIN (SELECT from_id, relation, to_id, MAX(checkpoint) AS c FROM relation_version WHERE checkpoint <= " parameter                    \
         " GROUP BY from_id, relation, to_id) m ON v.from_id = m.from_id AND v.relation = m.relation AND v.to_id = m.to_id AND v.checkpoint = m.c)"

constexpr char ITEM_DIFF_SQL[] =
    "WITH " ITEM_AT("f", "?1") ", " ITEM_AT("t", "?2") ", k AS (SELECT item_id FROM f UNION SELECT item_id FROM t) "
    "SELECT k.item_id, f.checkpoint, f.removed, t.checkpoint, t.removed, COALESCE(t.kind, f.kind), "
    "f.title IS NOT t.title, f.summary IS NOT t.summary, f.parent_id IS NOT t.parent_id, f.tags IS NOT t.tags, f.body IS NOT t.body "
    "FROM k LEFT JOIN f ON f.item_id = k.item_id LEFT JOIN t ON t.item_id = k.item_id "
    "ORDER BY " KIND_ORDER("COALESCE(t.kind, f.kind)") ", k.item_id";

constexpr char RELATION_DIFF_SQL[] =
    "WITH " RELATION_AT("f", "?1") ", " RELATION_AT("t", "?2") ", "
    "k AS (SELECT from_id, relation, to_id FROM f UNION SELECT from_id, relation, to_id FROM t) "
    "SELECT k.from_id, k.relation, k.to_id, COALESCE(t.model, f.model), f.removed, t.removed, t.checkpoint "
    "FROM k LEFT JOIN f ON f.from_id = k.from_id AND f.relation = k.relation AND f.to_id = k.to_id "
    "LEFT JOIN t ON t.from_id = k.from_id AND t.relation = k.relation AND t.to_id = k.to_id "
    "ORDER BY " KIND_ORDER("COALESCE(t.model, f.model)") ", k.from_id, k.relation, k.to_id";

constexpr char DECISION_DIFF_SQL[] =
    "SELECT k.kind, (SELECT MAX(checkpoint) FROM decision_version d WHERE d.kind = k.kind AND d.checkpoint <= ?1), "
    "(SELECT MAX(checkpoint) FROM decision_version d WHERE d.kind = k.kind AND d.checkpoint <= ?2) FROM (SELECT DISTINCT kind FROM decision_version) k";

constexpr char ITEM_AT_SQL[] =
    "SELECT checkpoint, kind, parent_id, title, summary, tags, removed FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 "
    "ORDER BY checkpoint DESC LIMIT 1";

constexpr char BODY_AT_SQL[] =
    "SELECT body, removed FROM item_version WHERE item_id = ?1 AND checkpoint <= ?2 ORDER BY checkpoint DESC LIMIT 1";

constexpr char DECISION_AT_SQL[] =
    "SELECT description FROM decision_version WHERE kind = ?1 AND checkpoint <= ?2 ORDER BY checkpoint DESC LIMIT 1";

bool inMask(uint32_t mask, uint32_t kind) { return mask == 0 || (kind >= 1 && kind <= 32 && (mask & (1u << (kind - 1))) != 0); }

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

KiottySpecStatus copyOut(Span<const char> text, char* buffer, size_t capacity, size_t& written)
{
    written = text.length();
    if (buffer == nullptr || capacity <= text.length()) {
        written = text.length() + 1;
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    std::memcpy(buffer, text.data(), text.length());
    buffer[text.length()] = '\0';
    return KIOTTY_SPEC_STATUS_SUCCESS;
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

template <typename Visit>
KiottySpecStatus Differ::eachItemDiff(DiffRange range, uint32_t kind_mask, uint32_t field_mask, Visit visit)
{
    auto query = _w.storage.prepare(literalText(ITEM_DIFF_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, static_cast<int64_t>(range.from));
    query.value.bind(2, static_cast<int64_t>(range.to));
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _w.storage.check(stepped.code); }
        if (!stepped.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        const Row row = query.value.row();
        const uint32_t kind = entity::parseKind(viewOf(row.text(5)));
        if (!inMask(kind_mask, kind)) { continue; }
        const bool before = !row.isNull(1) && row.integer(2) == 0;
        const bool after = !row.isNull(3) && row.integer(4) == 0;
        ItemDiffRow diff {};
        diff.id.assign(row.text(0));
        diff.kind = kind;
        diff.before = before ? static_cast<uint32_t>(row.integer(1)) : 0;
        diff.after = row.isNull(3) ? 0 : static_cast<uint32_t>(row.integer(3));
        if (!before && after) {
            diff.change = KIOTTY_SPEC_CHANGE_ADDED;
        } else if (before && !after) {
            diff.change = KIOTTY_SPEC_CHANGE_REMOVED;
        } else if (before && after && row.integer(1) != row.integer(3)) {
            const uint32_t flags[] = {KIOTTY_SPEC_CHANGED_FIELD_TITLE, KIOTTY_SPEC_CHANGED_FIELD_SUMMARY, KIOTTY_SPEC_CHANGED_FIELD_PARENT,
                                      KIOTTY_SPEC_CHANGED_FIELD_TAGS, KIOTTY_SPEC_CHANGED_FIELD_BODY};
            for (int index = 0; index < 5; ++index) {
                if (row.integer(6 + index) != 0) { diff.fields |= flags[index]; }
            }
            if (diff.fields == 0 || (field_mask != 0 && (diff.fields & field_mask) == 0)) { continue; }
            diff.change = KIOTTY_SPEC_CHANGE_MODIFIED;
        } else {
            continue;
        }
        const KiottySpecStatus visited = visit(diff);
        if (visited != KIOTTY_SPEC_STATUS_SUCCESS) { return visited; }
    }
}

template <typename Visit>
KiottySpecStatus Differ::eachRelationDiff(DiffRange range, uint32_t kind_mask, Visit visit)
{
    auto query = _w.storage.prepare(literalText(RELATION_DIFF_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, static_cast<int64_t>(range.from));
    query.value.bind(2, static_cast<int64_t>(range.to));
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _w.storage.check(stepped.code); }
        if (!stepped.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        const Row row = query.value.row();
        const uint32_t model = entity::parseKind(viewOf(row.text(3)));
        if (!inMask(kind_mask, model)) { continue; }
        const bool before = !row.isNull(4) && row.integer(4) == 0;
        const bool after = !row.isNull(5) && row.integer(5) == 0;
        if (before == after) { continue; }
        RelationDiffRow diff {};
        diff.from_id.assign(row.text(0));
        diff.to_id.assign(row.text(2));
        diff.relation = static_cast<uint32_t>(row.integer(1));
        diff.model = model;
        diff.checkpoint = static_cast<uint32_t>(row.integer(6));
        diff.change = after ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
        const KiottySpecStatus visited = visit(diff);
        if (visited != KIOTTY_SPEC_STATUS_SUCCESS) { return visited; }
    }
}

KiottySpecStatus Differ::summarize(DiffRange range, uint32_t kind_mask, uint32_t field_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT])
{
    for (KindCounts& count : counts) { count = KindCounts{}; }
    KiottySpecStatus status = eachItemDiff(range, kind_mask, field_mask, [&](const ItemDiffRow& diff) {
        KindCounts& count = counts[diff.kind - 1];
        if (diff.change == KIOTTY_SPEC_CHANGE_ADDED) { ++count.added; }
        if (diff.change == KIOTTY_SPEC_CHANGE_MODIFIED) { ++count.modified; }
        if (diff.change == KIOTTY_SPEC_CHANGE_REMOVED) { ++count.removed; }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = eachRelationDiff(range, kind_mask, [&](const RelationDiffRow& diff) {
        KindCounts& count = counts[diff.model - 1];
        if (diff.change == KIOTTY_SPEC_CHANGE_ADDED) { ++count.relations_added; } else { ++count.relations_removed; }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    auto query = _w.storage.prepare(literalText(DECISION_DIFF_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, static_cast<int64_t>(range.from));
    query.value.bind(2, static_cast<int64_t>(range.to));
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _w.storage.check(stepped.code); }
        if (!stepped.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        const Row row = query.value.row();
        const uint32_t kind = entity::parseKind(viewOf(row.text(0)));
        if (kind == 0 || !inMask(kind_mask, kind)) { continue; }
        const bool changed = row.isNull(1) != row.isNull(2) || (!row.isNull(1) && row.integer(1) != row.integer(2));
        if (changed) { counts[kind - 1].decision_changed = 1; }
    }
}

CursorIdResult Differ::items(DiffRange range, uint32_t kind_mask, uint32_t field_mask)
{
    const auto cursor = _cursors.open(CursorKind::ITEM_DIFF);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    const KiottySpecStatus status = eachItemDiff(range, kind_mask, field_mask, [&](const ItemDiffRow& diff) {
        CursorRow row;
        packId(diff.id, row.first, row.second);
        row.third = diff.before | (static_cast<uint64_t>(diff.after) << SHIFT);
        row.fourth = diff.kind | (static_cast<uint64_t>(diff.change) << 8) | (static_cast<uint64_t>(diff.fields) << 16);
        const KiottySpecStatus pushed = _cursors.push(cursor.value, row);
        if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
            return _w.failure.raise(pushed, {literalText("바뀐 항목이 max_cursor_rows 를 넘음. Kind 를 좁혀 다시 열 것")});
        }
        return pushed;
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(status);
    }
    return cursor;
}

CursorIdResult Differ::relations(DiffRange range, uint32_t kind_mask)
{
    const auto cursor = _cursors.open(CursorKind::RELATION_DIFF);
    if (!cursor) { return CursorIdResult::failure(_w.failure.raise(cursor.code, {literalText("커서 수가 max_cursors 에 닿음")})); }
    const KiottySpecStatus status = eachRelationDiff(range, kind_mask, [&](const RelationDiffRow& diff) {
        CursorRow row;
        packId(diff.from_id, row.first, row.second);
        packId(diff.to_id, row.third, row.fourth);
        row.number = static_cast<int64_t>(diff.checkpoint | (static_cast<uint64_t>(diff.relation & 0xFFFFu) << 32) |
                                          (static_cast<uint64_t>(diff.model) << 48) | (static_cast<uint64_t>(diff.change) << 56));
        const KiottySpecStatus pushed = _cursors.push(cursor.value, row);
        if (pushed == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
            return _w.failure.raise(pushed, {literalText("바뀐 관계가 max_cursor_rows 를 넘음. Kind 를 좁혀 다시 열 것")});
        }
        return pushed;
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        _cursors.close(cursor.value);
        return CursorIdResult::failure(status);
    }
    return cursor;
}

Result<ItemVersion, KiottySpecStatus> Differ::itemAt(Span<const char> id, uint32_t checkpoint)
{
    using VersionResult = Result<ItemVersion, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(ITEM_AT_SQL));
    if (!query) { return VersionResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return VersionResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(6) != 0) {
        return VersionResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    const Row row = query.value.row();
    ItemVersion version;
    version.record.checkpoint = entity::CheckpointHandle{static_cast<uint32_t>(row.integer(0)), 0};
    version.record.kind = static_cast<ItemKind>(entity::parseKind(viewOf(row.text(1))));
    version.record.parent = entity::INVALID_ITEM_HANDLE;
    version.record.id.assign(id);
    version.record.title.assign(row.text(3));
    version.record.summary.assign(row.text(4));
    if (!row.isNull(2)) {
        version.parent_id.assign(row.text(2));
        const auto parent = _w.items.find(row.text(2));
        if (parent) { version.record.parent = parent.value; }
    }
    return VersionResult::success(version);
}

Result<uint64_t, KiottySpecStatus> Differ::tagAt(Span<const char> id, uint32_t tag, uint32_t checkpoint)
{
    using TagResult = Result<uint64_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(ITEM_AT_SQL));
    if (!query) { return TagResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return TagResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(6) != 0) {
        return TagResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    // "[[1,4],[2,1]]" 에서 tag 의 값을 찾는다.
    const std::string_view text = viewOf(query.value.row().text(5));
    uint64_t numbers[2] {};
    size_t count = 0;
    uint64_t current = 0;
    bool digits = false;
    for (const char letter : text) {
        if (letter >= '0' && letter <= '9') {
            current = current * 10 + static_cast<uint64_t>(letter - '0');
            digits = true;
            continue;
        }
        if (digits && count < 2) { numbers[count++] = current; }
        current = 0;
        digits = false;
        if (letter == ']' && count == 2) {
            if (numbers[0] == tag) { return TagResult::success(numbers[1]); }
            count = 0;
        }
    }
    return TagResult::success(0);
}

Result<size_t, KiottySpecStatus> Differ::bodyAt(Span<const char> id, uint32_t checkpoint, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(BODY_AT_SQL));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, id);
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_w.storage.check(stepped.code)); }
    if (!stepped.value || query.value.row().integer(1) != 0) {
        return SizeResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("그 시점에 없는 항목: "), id}));
    }
    size_t written = 0;
    const KiottySpecStatus copied = copyOut(query.value.row().text(0), buffer, capacity, written);
    return SizeResult{copied, written};
}

Result<size_t, KiottySpecStatus> Differ::decisionAt(ItemKind kind, uint32_t checkpoint, char* buffer, size_t capacity)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    auto query = _w.storage.prepare(literalText(DECISION_AT_SQL));
    if (!query) { return SizeResult::failure(query.code); }
    query.value.bind(1, spanOf(entity::kindName(kind)));
    query.value.bind(2, static_cast<int64_t>(checkpoint));
    const auto stepped = query.value.step();
    if (!stepped) { return SizeResult::failure(_w.storage.check(stepped.code)); }
    const Span<const char> text = stepped.value ? query.value.row().text(0) : Span<const char>();
    size_t written = 0;
    const KiottySpecStatus copied = copyOut(text, buffer, capacity, written);
    return SizeResult{copied, written};
}

CursorIdResult Differ::openText(entity::ItemHandle item, uint32_t field, DiffRange range)
{
    if (field != KIOTTY_SPEC_TEXT_FIELD_TITLE && field != KIOTTY_SPEC_TEXT_FIELD_SUMMARY) {
        return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("글자 diff 는 제목이나 요약 하나에만 쓴다")}));
    }
    const auto record = _w.items.get(item);
    if (!record) { return CursorIdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_HANDLE, {literalText("항목 핸들이 무효임")})); }
    const IdText id = record.value->id;
    const auto before = itemAt(id.view(), range.from);
    const auto after = itemAt(id.view(), range.to);
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

Result<TextPiece, KiottySpecStatus> Differ::nextText(CursorId cursor)
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
