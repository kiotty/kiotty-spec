#include "kiotty_spec_adapter.h"

namespace kiotty::spec::adapter {

FilterResult QueryParser::parseKindFilter(KiottySpecItemKind kind) const
{
    const bool is_accepted_kind = kind == 0 || entity::isValidKind(kind);
    if (!is_accepted_kind) {
        return FilterResult::failure(raise(KIOTTY_SPEC_SUBJECT_KIND));
    }
    return FilterResult::success(static_cast<uint32_t>(kind));
}

FilterResult QueryParser::parseStepFilter(KiottySpecStep step) const
{
    const bool is_accepted_step = step == 0 || entity::isValidStep(step);
    if (!is_accepted_step) {
        return FilterResult::failure(raise(KIOTTY_SPEC_SUBJECT_STEP));
    }
    return FilterResult::success(static_cast<uint32_t>(step));
}

DirectionResult QueryParser::parseDirection(KiottySpecDirection direction) const
{
    const bool is_direction =
        direction >= KIOTTY_SPEC_DIRECTION_UPSTREAM && direction <= KIOTTY_SPEC_DIRECTION_BOTH;
    if (!is_direction) { return DirectionResult::failure(raise(KIOTTY_SPEC_SUBJECT_RELATION)); }
    return DirectionResult::success(direction);
}

FilterResult QueryParser::parseTextField(KiottySpecTextField field) const
{
    const bool is_text_field =
        field == KIOTTY_SPEC_TEXT_FIELD_TITLE || field == KIOTTY_SPEC_TEXT_FIELD_SUMMARY;
    if (!is_text_field) { return FilterResult::failure(raise(KIOTTY_SPEC_SUBJECT_TEXT_FIELD)); }
    return FilterResult::success(static_cast<uint32_t>(field));
}

TagQueryResult QueryParser::parseTagQuery(KiottySpecTagQuery query) const
{
    const bool is_valid_kind = entity::isValidKind(query.kind);
    if (!is_valid_kind) { return TagQueryResult::failure(raise(KIOTTY_SPEC_SUBJECT_KIND)); }
    const entity::ItemKind kind = static_cast<entity::ItemKind>(query.kind);
    const size_t tag_count = _context.readSide().catalog().model(kind).tags.length;
    if (query.tag_index >= tag_count) {
        return TagQueryResult::failure(raise(KIOTTY_SPEC_SUBJECT_TAG));
    }
    return TagQueryResult::success(impl::TagQuery {query.mask, kind, query.tag_index});
}

FilterResult QueryParser::parseRelationIndex(entity::ItemKind kind, uint32_t relation) const
{
    const size_t relation_count = _context.readSide().catalog().model(kind).relations.length;
    if (relation >= relation_count) {
        return FilterResult::failure(raise(KIOTTY_SPEC_SUBJECT_RELATION));
    }
    return FilterResult::success(relation);
}

KiottySpecStatus QueryParser::raise(KiottySpecSubject subject) const
{ return _context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, subject); }

RangeResult RangeParser::parseRange(const KiottySpecDiffQuery* query) const
{
    if (query == nullptr) {
        return RangeResult::failure(_context.failure().raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                             KIOTTY_SPEC_SUBJECT_DIFF_RANGE));
    }
    const ValueParser parser(_context);
    const SequenceResult from = parser.parseCheckpoint(query->from);
    if (!from) { return RangeResult::failure(from.code); }
    const SequenceResult to = parser.parseCheckpoint(query->to);
    if (!to) { return RangeResult::failure(to.code); }
    return _context.readSide().differ().range(from.value, to.value);
}

SequenceResult RangeParser::parsePoint(KiottySpecCheckpointHandle checkpoint) const
{
    const SequenceResult parsed = ValueParser(_context).parseCheckpoint(checkpoint);
    if (!parsed) { return parsed; }
    return _context.readSide().differ().point(parsed.value);
}

KindResult parseKindCode(KiottySpecItemKind kind)
{
    const bool is_valid_kind = entity::isValidKind(kind);
    if (!is_valid_kind) { return KindResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return KindResult::success(static_cast<entity::ItemKind>(kind));
}

StateResult parseStateCode(KiottySpecCheckpointState state)
{
    const bool is_state =
        state >= KIOTTY_SPEC_CHECKPOINT_STATE_STARTED && state <= KIOTTY_SPEC_CHECKPOINT_STATE_DONE;
    if (!is_state) { return StateResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return StateResult::success(static_cast<entity::CheckpointState>(state));
}

}
