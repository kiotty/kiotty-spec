#include "implementation/domain/usecases/kiotty_relation_editor.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;
using entity::RelationRecord;

}

Result<RelationRecord, KiottySpecStatus> RelationEditor::relationOf(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    using RelationResult = Result<RelationRecord, KiottySpecStatus>;
    const auto from = _w.resolve(from_id);
    if (!from) { return RelationResult::failure(from.code); }
    const auto to = _w.resolve(to_id);
    if (!to) { return RelationResult::failure(to.code); }
    const ItemKind from_kind = _w.items.reader().get(from.value).value->kind;
    const ItemKind to_kind = _w.items.reader().get(to.value).value->kind;
    const KiottySpecStatus allowed = _w.catalog.checkRelation(from_kind, relation, to_kind);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return RelationResult::failure(
            _w.failure.raise(allowed, {literalText("모델의 관계 정의에 없는 관계: "), from_id, literalText(" -> "), to_id}));
    }
    return RelationResult::success(RelationRecord{from.value, to.value, from_kind, relation});
}

KiottySpecStatus RelationEditor::add(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    if (_w.relations.contains(record.value)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 있는 관계")}); }
    return _lifecycle.write([&](uint32_t sequence) { return _w.relations.add(record.value, sequence); });
}

KiottySpecStatus RelationEditor::remove(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    if (!_w.relations.contains(record.value)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 관계")}); }
    return _lifecycle.write([&](uint32_t sequence) { return _w.relations.remove(record.value, sequence); });
}

}
