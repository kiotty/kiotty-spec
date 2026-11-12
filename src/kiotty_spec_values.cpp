#include "kiotty_spec_adapter.h"

namespace kiotty::spec::adapter {

KindResult ValueParser::parseKind(KiottySpecItemKind kind) const
{
    const bool is_valid_kind = entity::isValidKind(kind);
    if (!is_valid_kind) {
        return KindResult::failure(
            raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_KIND));
    }
    return KindResult::success(static_cast<entity::ItemKind>(kind));
}

KindResult ValueParser::parseDecisionKind(KiottySpecItemKind kind) const
{
    const bool is_valid_kind = entity::isValidKind(kind);
    const bool has_decision =
        is_valid_kind && entity::hasDecision(static_cast<entity::ItemKind>(kind));
    if (!has_decision) {
        return KindResult::failure(
            raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_KIND));
    }
    return KindResult::success(static_cast<entity::ItemKind>(kind));
}

StepResult ValueParser::parseStep(KiottySpecStep step) const
{
    const bool is_valid_step = entity::isValidStep(step);
    if (!is_valid_step) {
        return StepResult::failure(
            raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_STEP));
    }
    return StepResult::success(static_cast<entity::Step>(step));
}

ActorResult ValueParser::parseActor(KiottySpecActor actor) const
{
    const bool is_valid_actor = entity::isValidActor(actor);
    if (!is_valid_actor) {
        return ActorResult::failure(
            raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_ACTOR));
    }
    return ActorResult::success(static_cast<entity::Actor>(actor));
}

SequenceResult ValueParser::parseCheckpoint(KiottySpecCheckpointHandle handle) const
{
    const bool has_high_bits = (handle >> SHIFT) != 0;
    if (has_high_bits) {
        return SequenceResult::failure(
            raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
    }
    return SequenceResult::success(static_cast<uint32_t>(handle));
}

TagResult ValueParser::parseTag(KiottySpecItemHandle item, uint32_t tag, uint64_t value) const
{
    const RecordResult record = Boundary(_context).read(item);
    if (!record) { return TagResult::failure(record.code); }
    const entity::ItemKind kind = record.value->kind;
    const entity::ItemHandle handle = fromHandle(item);
    const impl::Span<const char> id =
        _context.workspace().items.reader().label(handle).value->id.view();
    const KiottySpecStatus known = Boundary(_context).checkTag(kind, tag, id);
    if (known != KIOTTY_SPEC_STATUS_SUCCESS) { return TagResult::failure(known); }
    const KiottySpecStatus allowed = _context.workspace().catalog.checkTagValue(kind, tag, value);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return TagResult::failure(
            _context.failure().raise(allowed, KIOTTY_SPEC_SUBJECT_TAG_VALUE, id));
    }
    entity::TagRecord parsed {};
    parsed.value = value;
    parsed.item = handle;
    parsed.model = kind;
    parsed.tag = tag;
    return TagResult::success(parsed);
}

RelationResult ValueParser::parseRelation(KiottySpecText from_id, uint32_t relation,
                                          KiottySpecText to_id) const
{
    const Boundary boundary(_context);
    const HandleResult from = boundary.resolve(from_id);
    if (!from) { return RelationResult::failure(from.code); }
    const HandleResult to = boundary.resolve(to_id);
    if (!to) { return RelationResult::failure(to.code); }
    const impl::ItemReader reader = _context.workspace().items.reader();
    const entity::ItemKind from_kind = reader.get(from.value).value->kind;
    const entity::ItemKind to_kind = reader.get(to.value).value->kind;
    const KiottySpecStatus allowed =
        _context.workspace().catalog.checkRelation(from_kind, relation, to_kind);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return RelationResult::failure(_context.failure().raise(
            allowed, KIOTTY_SPEC_SUBJECT_RELATION, reader.label(from.value).value->id.view()));
    }
    return RelationResult::success(entity::RelationRecord {from.value, to.value, from_kind,
                                                           relation});
}

KiottySpecStatus ValueParser::raise(KiottySpecStatus code, KiottySpecSubject subject) const
{ return _context.failure().raise(code, subject); }

}
