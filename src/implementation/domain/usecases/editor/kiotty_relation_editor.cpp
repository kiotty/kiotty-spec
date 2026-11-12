#include "implementation/domain/usecases/editor/kiotty_relation_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/workspace/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::RelationRecord;

}

KiottySpecStatus RelationEditor::add(const RelationRecord& relation)
{
    const bool has_relation = _workspace.relations.contains(relation);
    if (has_relation) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_RELATION,
                                        idOf(relation.from));
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.relations.writer().add(relation, sequence.value));
}

KiottySpecStatus RelationEditor::remove(const RelationRecord& relation)
{
    const bool has_relation = _workspace.relations.contains(relation);
    if (!has_relation) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_RELATION,
                                        idOf(relation.from));
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.relations.writer().remove(relation, sequence.value));
}

Span<const char> RelationEditor::idOf(ItemHandle item) const
{ return (*_workspace.items.reader().label(item).value).id.view(); }

}
