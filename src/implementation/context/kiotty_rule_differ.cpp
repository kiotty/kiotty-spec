#include "implementation/context/kiotty_rule_differ.h"

#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_line_diff.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_compare.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using RecordResult = Result<RuleDiffRecord, KiottySpecStatus>;
using CountResult = Result<size_t, KiottySpecStatus>;

constexpr unsigned char CONTINUATION_MASK = 0xC0u;
constexpr unsigned char CONTINUATION_BITS = 0x80u;

struct RuleStep {
    RuleDiffRecord record {};
    int order {0};
    bool has_row {false};
};

static_assert(sizeof(RuleStep) == 160, "unexpected padding");

bool hasMoreRules(RulePair pair, RuleOrder walk);
RuleStep stepAt(RulePair pair, RuleOrder walk);
RuleDiffRecord makeRecord(RuleSide side, KiottySpecChange change, uint32_t fields);
size_t pieceEnd(Span<const char> text, size_t start, size_t end);
CursorRow packPiece(TextSegment segment);
TextSegment unpackPiece(const CursorRow& row);

}

CursorIdResult RuleDiffer::open(Workspace& remote)
{
    const KiottySpecStatus free = checkFree(_store.active().rule, CursorKind::RULE_DIFF);
    if (free != KIOTTY_SPEC_STATUS_SUCCESS) { return CursorIdResult::failure(free); }
    const CountResult count = collectRows(RulePair {_local, remote});
    if (!count) { return CursorIdResult::failure(count.code); }
    const auto cursor = _cursors.open(CursorKind::RULE_DIFF);
    CursorWriter writer(_cursors, _local.failure);
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const KiottySpecStatus pushed = pushRows(cursor.value, count.value);
    if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return writer.closeWith(cursor.value, pushed); }
    _store.active().rule = cursor.value;
    return cursor;
}

Result<RuleDiffRecord, KiottySpecStatus> RuleDiffer::next(CursorId cursor)
{
    const CursorRowsResult rows = _cursors.fetch(cursor, CursorKind::RULE_DIFF, 1);
    if (!rows) { return RecordResult::failure(rows.code); }
    const Result<const CursorRow&, KiottySpecStatus> row = rows.value.at(0);
    if (!row) { return RecordResult::success(RuleDiffRecord {}); }
    const auto record = _store.rows().at((*row.value).first);
    if (!record) { return RecordResult::failure(record.code); }
    return RecordResult::success(*record.value);
}

CursorIdResult RuleDiffer::openBody(Workspace& remote, Span<const char> id)
{
    const KiottySpecStatus free = checkFree(_store.active().body, CursorKind::BODY_DIFF);
    if (free != KIOTTY_SPEC_STATUS_SUCCESS) { return CursorIdResult::failure(free); }
    const auto local_rule = _local.items.reader().find(id);
    const auto remote_rule = remote.items.reader().find(id);
    const bool has_any = local_rule || remote_rule;
    if (!has_any) {
        return CursorIdResult::failure(
            _local.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id));
    }
    const Span<const char> before = local_rule ? _local.items.reader().body(local_rule.value)
                                               : Span<const char>();
    const Span<const char> after = remote_rule ? remote.items.reader().body(remote_rule.value)
                                               : Span<const char>();
    const auto cursor = _cursors.open(CursorKind::BODY_DIFF);
    CursorWriter writer(_cursors, _local.failure);
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const KiottySpecStatus filled = fillBody(cursor.value, TextPair {before, after});
    if (filled != KIOTTY_SPEC_STATUS_SUCCESS) {
        return writer.closeWith(cursor.value, writer.raiseRowsFull(filled));
    }
    _store.active().body = cursor.value;
    return cursor;
}

Result<TextPiece, KiottySpecStatus> RuleDiffer::nextPiece(CursorId cursor)
{
    using PieceResult = Result<TextPiece, KiottySpecStatus>;
    const CursorRowsResult rows = _cursors.fetch(cursor, CursorKind::BODY_DIFF, 1);
    if (!rows) { return PieceResult::failure(rows.code); }
    const Result<const CursorRow&, KiottySpecStatus> row = rows.value.at(0);
    if (!row) { return PieceResult::success(TextPiece {}); }
    const TextSegment segment = unpackPiece(*row.value);
    const auto text = _store.textOf(segment.source);
    if (!text) { return PieceResult::failure(text.code); }
    const auto piece = text.value.subspan(segment.offset, segment.length);
    if (!piece) { return PieceResult::failure(piece.code); }
    return PieceResult::success(TextPiece {piece.value, static_cast<uint32_t>(segment.change)});
}

KiottySpecStatus RuleDiffer::checkFree(const std::optional<CursorId>& active, CursorKind kind)
{
    const bool has_active = active.has_value();
    if (!has_active) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto located = _cursors.locate(*active, kind);
    if (!located) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _local.failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CURSOR);
}

Result<size_t, KiottySpecStatus> RuleDiffer::collectRows(RulePair pair)
{
    const Span<RuleDiffRecord> rows = _store.rows();
    RuleOrder walk {skipToRule(pair.local, 0), skipToRule(pair.remote, 0)};
    size_t count = 0;
    while (hasMoreRules(pair, walk)) {
        const RuleStep step = stepAt(pair, walk);
        walk = advanceOrder(pair, walk, step.order);
        if (!step.has_row) { continue; }
        if (count == rows.length()) {
            return CountResult::failure(_local.failure.raise(
                KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, KIOTTY_SPEC_SUBJECT_CURSOR_ROWS));
        }
        rows[count] = step.record;
        ++count;
    }
    return CountResult::success(count);
}

KiottySpecStatus RuleDiffer::pushRows(CursorId cursor, size_t count)
{
    CursorWriter writer(_cursors, _local.failure);
    for (size_t index = 0; index < count; ++index) {
        CursorRow row {};
        row.first = index;
        const KiottySpecStatus pushed = writer.pushRow(cursor, row);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleDiffer::fillBody(CursorId cursor, TextPair texts)
{
    const KiottySpecStatus stored = storeBodies(texts);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) { return stored; }
    const Span<TextSegment> segments = _store.segments();
    const CountResult count = diffLines(texts.before, texts.after, _store.lineWork(), segments);
    if (!count) { return count.code; }
    const auto written = segments.subspan(0, count.value);
    if (!written) { return written.code; }
    return pushPieces(cursor, written.value);
}

KiottySpecStatus RuleDiffer::storeBodies(TextPair texts)
{
    const auto before_slot = _store.textOf(TextSource::BEFORE);
    if (!before_slot) { return before_slot.code; }
    const auto after_slot = _store.textOf(TextSource::AFTER);
    if (!after_slot) { return after_slot.code; }
    const KiottySpecStatus before_copied = before_slot.value.copyFrom(texts.before);
    if (before_copied != KIOTTY_SPEC_STATUS_SUCCESS) { return before_copied; }
    return after_slot.value.copyFrom(texts.after);
}

KiottySpecStatus RuleDiffer::pushPieces(CursorId cursor, Span<const TextSegment> segments)
{
    for (const TextSegment segment : segments) {
        const auto text = _store.textOf(segment.source);
        if (!text) { return text.code; }
        const size_t end = segment.offset + segment.length;
        size_t start = segment.offset;
        while (start < end) {
            const size_t stop = pieceEnd(text.value, start, end);
            TextSegment piece = segment;
            piece.offset = static_cast<uint16_t>(start);
            piece.length = static_cast<uint16_t>(stop - start);
            const KiottySpecStatus pushed = _cursors.push(cursor, packPiece(piece));
            if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
            start = stop;
        }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

bool hasMoreRules(RulePair pair, RuleOrder walk)
{
    const bool has_local = walk.local < pair.local.items.slots().count();
    const bool has_remote = walk.remote < pair.remote.items.slots().count();
    return has_local || has_remote;
}

RuleStep stepAt(RulePair pair, RuleOrder walk)
{
    const RuleSide local {pair.local, ruleAt(pair.local, walk.local)};
    const RuleSide remote {pair.remote, ruleAt(pair.remote, walk.remote)};
    RuleStep step {};
    step.order = compareOrder(local, remote, walk);
    if (step.order < 0) { step.record = makeRecord(local, KIOTTY_SPEC_CHANGE_REMOVED, 0); }
    if (step.order > 0) { step.record = makeRecord(remote, KIOTTY_SPEC_CHANGE_ADDED, 0); }
    const uint32_t fields = step.order == 0 ? compareRules(local, remote) : 0;
    if (fields != 0) { step.record = makeRecord(remote, KIOTTY_SPEC_CHANGE_MODIFIED, fields); }
    step.has_row = step.order != 0 || fields != 0;
    return step;
}

RuleDiffRecord makeRecord(RuleSide side, KiottySpecChange change, uint32_t fields)
{
    RuleDiffRecord record {};
    const auto label = side.workspace.items.reader().label(side.handle);
    if (label) {
        record.id = (*label.value).id;
        record.title = (*label.value).title;
    }
    record.change = static_cast<uint32_t>(change);
    record.changed_fields = fields;
    return record;
}

size_t pieceEnd(Span<const char> text, size_t start, size_t end)
{
    const size_t limit = start + MAX_DIFF_TEXT_BYTES;
    if (limit >= end) { return end; }
    size_t stop = limit;
    while (stop > start) {
        const unsigned char letter = static_cast<unsigned char>(text[stop]);
        const bool is_continuation = (letter & CONTINUATION_MASK) == CONTINUATION_BITS;
        if (!is_continuation) { return stop; }
        --stop;
    }
    return limit;
}

CursorRow packPiece(TextSegment segment)
{
    CursorRow row {};
    row.first = segment.offset;
    row.second = segment.length;
    row.third = static_cast<uint64_t>(segment.change);
    row.fourth = static_cast<uint64_t>(segment.source);
    return row;
}

TextSegment unpackPiece(const CursorRow& row)
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
