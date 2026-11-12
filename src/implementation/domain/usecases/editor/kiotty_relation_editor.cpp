#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/workspace/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;
using entity::RelationRecord;

}

KiottySpecStatus RelationEditor::add(Span<const char> from_id, uint32_t relation,
                                     Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    const bool has_relation = _workspace.relations.contains(record.value);
    if (has_relation) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_RELATION,
                                        from_id);
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.relations.writer().add(record.value, sequence.value));
}

KiottySpecStatus RelationEditor::remove(Span<const char> from_id, uint32_t relation,
                                        Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    const bool has_relation = _workspace.relations.contains(record.value);
    if (!has_relation) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_RELATION,
                                        from_id);
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.relations.writer().remove(record.value, sequence.value));
}

Result<RelationRecord, KiottySpecStatus>
RelationEditor::relationOf(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    using RelationResult = Result<RelationRecord, KiottySpecStatus>;
    const auto from = _workspace.resolve(from_id);
    if (!from) { return RelationResult::failure(from.code); }
    const auto to = _workspace.resolve(to_id);
    if (!to) { return RelationResult::failure(to.code); }
    const ItemKind from_kind = (*_workspace.items.reader().get(from.value).value).kind;
    const ItemKind to_kind = (*_workspace.items.reader().get(to.value).value).kind;
    const KiottySpecStatus allowed = _workspace.catalog.checkRelation(from_kind, relation, to_kind);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return RelationResult::failure(
            _workspace.failure.raise(allowed, KIOTTY_SPEC_SUBJECT_RELATION, from_id));
    }
    return RelationResult::success(RelationRecord{from.value, to.value, from_kind, relation});
}

}
