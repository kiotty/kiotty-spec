#include "implementation/context/kiotty_rule_differ.h"

#include "implementation/context/kiotty_cursor_rows.h"
#include "implementation/core/kiotty_line_diff.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using RecordResult = Result<RuleDiffRecord, KiottySpecStatus>;
using CountResult = Result<size_t, KiottySpecStatus>;
using HandleResult = Result<ItemHandle, KiottySpecStatus>;
using LabelResult = Result<const entity::ItemLabel&, KiottySpecStatus>;

constexpr unsigned char CONTINUATION_MASK = 0xC0u;
constexpr unsigned char CONTINUATION_BITS = 0x80u;

struct RuleStep {
    RuleDiffRecord record {};
    int order {0};
    uint32_t has_row {0};
};

static_assert(sizeof(RuleStep) == 160, "unexpected padding");

bool hasMoreRules(RulePair pair, RuleOrder walk);
RuleStep compareStep(RulePair pair, RuleOrder walk);
RuleDiffRecord makeRecord(RuleSide side, KiottySpecChange change, uint32_t fields);
Span<const char> bodyOf(const Workspace& workspace, const HandleResult& rule);
size_t findPieceEnd(Span<const char> text, size_t start, size_t end);
CursorRow packPiece(TextSegment segment);
TextSegment unpackPiece(const CursorRow& row);

}

CursorIdResult RuleDiffer::open(const Workspace& remote)
{
    const KiottySpecStatus free = checkFree(_store.active().rule, CursorKind::RULE_DIFF);
    if (free != KIOTTY_SPEC_STATUS_SUCCESS) { return CursorIdResult::failure(free); }
    const CountResult count = collectRows(RulePair {_local, remote});
    if (!count) { return CursorIdResult::failure(count.code); }
    const CursorIdResult cursor = _cursors.open(CursorKind::RULE_DIFF);
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
    const Result<RuleDiffRecord&, KiottySpecStatus> record = _store.rows().at((*row.value).first);
    if (!record) { return RecordResult::failure(record.code); }
    return RecordResult::success(*record.value);
}

CursorIdResult RuleDiffer::openBody(const Workspace& remote, Span<const char> id)
{
    const KiottySpecStatus free = checkFree(_store.active().body, CursorKind::BODY_DIFF);
    if (free != KIOTTY_SPEC_STATUS_SUCCESS) { return CursorIdResult::failure(free); }
    const HandleResult local_rule = _local.items.reader().find(id);
    const HandleResult remote_rule = remote.items.reader().find(id);
    const bool has_any = local_rule || remote_rule;
    if (!has_any) {
        return CursorIdResult::failure(
            _local.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id));
    }
    const TextPair texts {bodyOf(_local, local_rule), bodyOf(remote, remote_rule)};
    const CursorIdResult cursor = _cursors.open(CursorKind::BODY_DIFF);
    CursorWriter writer(_cursors, _local.failure);
    if (!cursor) { return CursorIdResult::failure(writer.raiseCursorFull(cursor.code)); }
    const KiottySpecStatus filled = fillBody(cursor.value, texts);
    if (filled != KIOTTY_SPEC_STATUS_SUCCESS) {
        return writer.closeWith(cursor.value, writer.raiseRowsFull(filled));
    }
    _store.active().body = cursor.value;
    return cursor;
}

Result<TextPiece, KiottySpecStatus> RuleDiffer::fetchPiece(CursorId cursor)
{
    using PieceResult = Result<TextPiece, KiottySpecStatus>;
    const CursorRowsResult rows = _cursors.fetch(cursor, CursorKind::BODY_DIFF, 1);
    if (!rows) { return PieceResult::failure(rows.code); }
    const Result<const CursorRow&, KiottySpecStatus> row = rows.value.at(0);
    if (!row) { return PieceResult::success(TextPiece {}); }
    const TextSegment segment = unpackPiece(*row.value);
    const Result<Span<char>, KiottySpecStatus> text = _store.textOf(segment.source);
    if (!text) { return PieceResult::failure(text.code); }
    const Result<Span<char>, KiottySpecStatus> piece =
        text.value.subspan(segment.offset, segment.length);
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
    const size_t total = pair.local.items.slots().count() + pair.remote.items.slots().count();
    RuleOrder walk {skipToRule(pair.local, 0), skipToRule(pair.remote, 0)};
    size_t count = 0;
    for (size_t step = 0; step < total; ++step) {
        const bool has_more = hasMoreRules(pair, walk);
        if (!has_more) { break; }
        const RuleStep compared = compareStep(pair, walk);
        walk = advanceOrder(pair, walk, compared.order);
        const bool is_kept = compared.has_row != 0;
        if (!is_kept) { continue; }
        const Result<RuleDiffRecord&, KiottySpecStatus> slot = rows.at(count);
        if (!slot) {
            return CountResult::failure(_local.failure.raise(
                KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, KIOTTY_SPEC_SUBJECT_CURSOR_ROWS));
        }
        *slot.value = compared.record;
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

KiottySpecStatus RuleDiffer::fillBody(CursorId cursor, const TextPair& texts)
{
    const KiottySpecStatus stored = storeBodies(texts);
    if (stored != KIOTTY_SPEC_STATUS_SUCCESS) { return stored; }
    const Span<TextSegment> segments = _store.segments();
    const CountResult count = diffLines(texts.before, texts.after, _store.lineWork(), segments);
    if (!count) { return count.code; }
    const Result<Span<TextSegment>, KiottySpecStatus> written = segments.subspan(0, count.value);
    if (!written) { return written.code; }
    return pushPieces(cursor, written.value);
}

KiottySpecStatus RuleDiffer::storeBodies(const TextPair& texts)
{
    const Result<Span<char>, KiottySpecStatus> before = _store.textOf(TextSource::BEFORE);
    if (!before) { return before.code; }
    const Result<Span<char>, KiottySpecStatus> after = _store.textOf(TextSource::AFTER);
    if (!after) { return after.code; }
    const KiottySpecStatus before_copied = before.value.copyFrom(texts.before);
    if (before_copied != KIOTTY_SPEC_STATUS_SUCCESS) { return before_copied; }
    return after.value.copyFrom(texts.after);
}

KiottySpecStatus RuleDiffer::pushPieces(CursorId cursor, Span<const TextSegment> segments)
{
    for (const TextSegment segment : segments) {
        const KiottySpecStatus pushed = pushSegment(cursor, segment);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleDiffer::pushSegment(CursorId cursor, TextSegment segment)
{
    const Result<Span<char>, KiottySpecStatus> text = _store.textOf(segment.source);
    if (!text) { return text.code; }
    const size_t end = static_cast<size_t>(segment.offset) + segment.length;
    size_t start = segment.offset;
    for (size_t piece_count = 0; piece_count < segment.length; ++piece_count) {
        const bool is_done = start >= end;
        if (is_done) { break; }
        const size_t stop = findPieceEnd(text.value, start, end);
        TextSegment piece = segment;
        piece.offset = static_cast<uint16_t>(start);
        piece.length = static_cast<uint16_t>(stop - start);
        const KiottySpecStatus pushed = _cursors.push(cursor, packPiece(piece));
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
        start = stop;
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

RuleStep compareStep(RulePair pair, RuleOrder walk)
{
    const RuleSide local {pair.local, ruleOf(pair.local, walk.local)};
    const RuleSide remote {pair.remote, ruleOf(pair.remote, walk.remote)};
    RuleStep step {};
    step.order = compareOrder(local, remote, walk);
    const uint32_t fields = step.order == 0 ? compareRules(local, remote) : 0;
    const bool is_removed = step.order < 0;
    const bool is_added = step.order > 0;
    const bool is_modified = fields != 0;
    if (is_removed) { step.record = makeRecord(local, KIOTTY_SPEC_CHANGE_REMOVED, 0); }
    if (is_added) { step.record = makeRecord(remote, KIOTTY_SPEC_CHANGE_ADDED, 0); }
    if (is_modified) { step.record = makeRecord(remote, KIOTTY_SPEC_CHANGE_MODIFIED, fields); }
    const bool has_row = is_removed || is_added || is_modified;
    step.has_row = has_row ? 1u : 0u;
    return step;
}

RuleDiffRecord makeRecord(RuleSide side, KiottySpecChange change, uint32_t fields)
{
    RuleDiffRecord record {};
    const LabelResult label = side.workspace.items.reader().label(side.handle);
    if (label) {
        record.id = (*label.value).id;
        record.title = (*label.value).title;
    }
    record.change = static_cast<uint32_t>(change);
    record.changed_fields = fields;
    return record;
}

Span<const char> bodyOf(const Workspace& workspace, const HandleResult& rule)
{
    if (!rule) { return Span<const char>(); }
    return workspace.items.reader().bodyOf(rule.value);
}

size_t findPieceEnd(Span<const char> text, size_t start, size_t end)
{
    const size_t limit = start + MAX_DIFF_TEXT_BYTES;
    const bool is_short = limit >= end;
    if (is_short) { return end; }
    for (size_t stop = limit; stop > start; --stop) {
        const Result<const char&, KiottySpecStatus> letter = text.at(stop);
        const unsigned char code = letter ? static_cast<unsigned char>(*letter.value) : 0u;
        const bool is_continuation = (code & CONTINUATION_MASK) == CONTINUATION_BITS;
        if (!is_continuation) { return stop; }
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
