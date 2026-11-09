#include "implementation/domain/usecases/kiotty_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/domain/entity/kiotty_item_id.h"

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::RelationRecord;

}

template <typename Operation>
KiottySpecStatus Editor::write(Operation operation)
{
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    bool touched = false;
    {
        Savepoint savepoint(_w.storage.connection());
        status = _w.storage.check(savepoint.begin());
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            const auto sequence = _lifecycle.beginWrite();
            touched = true;
            status = sequence ? operation(sequence.value) : sequence.code;
        }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _w.storage.check(savepoint.commit()); }
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS && touched) { _w.reload(); }
    return status;
}

KiottySpecStatus Editor::checkText(Span<const char> text, size_t limit, const char* field)
{
    if (text.length() > limit) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {boundedText(field, 32), literalText(" 이 상한을 넘음")});
    }
    if (!isValidUtf8(text)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_UTF8, {boundedText(field, 32), literalText(" 이 UTF-8 이 아님")}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<ItemHandle, KiottySpecStatus> Editor::resolve(Span<const char> id)
{
    const auto found = _w.items.reader().find(id);
    if (!found) { return Result<ItemHandle, KiottySpecStatus>::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 항목: "), id})); }
    return found;
}

KiottySpecStatus Editor::markNeedsChange(ItemHandle item, uint32_t sequence)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return record.code; }
    const ItemKind kind = record.value->kind;
    if (kind != ItemKind::DESIGN && kind != ItemKind::UNIT_TEST_PLAN) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto state = _w.catalog.findTag(kind, literalText("구현 상태"));
    if (!state) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto done = _w.catalog.findTagBit(kind, state.value, literalText("구현 완료"));
    const auto changed = _w.catalog.findTagBit(kind, state.value, literalText("변경 필요"));
    if (!done || !changed) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (_w.items.tags().value(item, state.value) != (uint64_t{1} << done.value)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _w.items.writer().setTag(item, state.value, uint64_t{1} << changed.value, sequence);
}

Result<IdText, KiottySpecStatus> Editor::createItem(ItemKind kind, Span<const char> title, Span<const char> summary)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    if (!entity::isValidKind(static_cast<uint32_t>(kind))) {
        return IdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 Kind")}));
    }
    if (title.length() == 0) { return IdResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("제목이 비어 있음")})); }
    KiottySpecStatus status = checkText(title, entity::MAX_TITLE_LENGTH, "제목");
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = checkText(summary, entity::MAX_SUMMARY_LENGTH, "요약"); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    IdText created;
    status = write([&](uint32_t sequence) {
        const auto id = _w.meta.nextId(_w.catalog.model(kind).id_prefix.view());
        if (!id) { return id.code; }
        ItemRecord record {};
        record.parent = entity::INVALID_ITEM_HANDLE;
        record.kind = kind;
        record.id = id.value;
        record.title.assign(title);
        record.summary.assign(summary);
        const auto handle = _w.items.writer().create(record, sequence);
        if (!handle) { return handle.code; }
        created = id.value;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return IdResult::success(created);
}

KiottySpecStatus Editor::updateItem(Span<const char> id, Span<const char> title, Span<const char> summary)
{
    const auto item = resolve(id);
    if (!item) { return item.code; }
    ItemRecord record = *_w.items.reader().get(item.value).value;
    if (title.data() != nullptr) {
        if (title.length() == 0) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("제목이 비어 있음")}); }
        const KiottySpecStatus checked = checkText(title, entity::MAX_TITLE_LENGTH, "제목");
        if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
        record.title.assign(title);
    }
    if (summary.data() != nullptr) {
        const KiottySpecStatus checked = checkText(summary, entity::MAX_SUMMARY_LENGTH, "요약");
        if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
        record.summary.assign(summary);
    }
    return write([&](uint32_t sequence) {
        const KiottySpecStatus updated = _w.items.writer().update(item.value, record, sequence);
        if (updated != KIOTTY_SPEC_STATUS_SUCCESS) { return updated; }
        return markNeedsChange(item.value, sequence);
    });
}

KiottySpecStatus Editor::setParent(Span<const char> id, Span<const char> parent_id)
{
    const auto item = resolve(id);
    if (!item) { return item.code; }
    ItemRecord record = *_w.items.reader().get(item.value).value;
    record.parent = entity::INVALID_ITEM_HANDLE;
    if (parent_id.length() > 0) {
        const auto parent = resolve(parent_id);
        if (!parent) { return parent.code; }
        for (ItemHandle probe = parent.value; probe.isValid();) {
            if (probe.index == item.value.index) {
                return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("상위 항목이 순환함: "), id});
            }
            const auto above = _w.items.reader().get(probe);
            if (!above) { break; }
            probe = above.value->parent;
        }
        record.parent = parent.value;
    }
    return write([&](uint32_t sequence) { return _w.items.writer().update(item.value, record, sequence); });
}

KiottySpecStatus Editor::removeItem(Span<const char> id)
{
    const auto item = resolve(id);
    if (!item) { return item.code; }
    for (size_t index = 0; index < _w.items.reader().capacity(); ++index) {
        const auto child = _w.items.reader().get(_w.items.reader().handleAt(index));
        if (child && child.value->parent.index == item.value.index && child.value->parent.isValid()) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("하위 항목이 있어 지울 수 없음: "), child.value->id.view()});
        }
    }
    return write([&](uint32_t sequence) {
        const KiottySpecStatus unlinked = _w.relations.removeAllOf(item.value, sequence);
        if (unlinked != KIOTTY_SPEC_STATUS_SUCCESS) { return unlinked; }
        return _w.items.writer().remove(item.value, sequence);
    });
}

KiottySpecStatus Editor::writeBody(Span<const char> id, Span<const char> body)
{
    const auto item = resolve(id);
    if (!item) { return item.code; }
    const KiottySpecStatus checked = checkText(body, entity::MAX_BODY_LENGTH, "본문");
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    return write([&](uint32_t sequence) {
        const KiottySpecStatus written = _w.items.writer().writeBody(item.value, body, sequence);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
        return markNeedsChange(item.value, sequence);
    });
}

KiottySpecStatus Editor::setTag(ItemHandle item, uint32_t tag, uint64_t value)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return _w.failure.raise(record.code, {literalText("항목 핸들이 무효임")}); }
    const KiottySpecStatus allowed = _w.catalog.checkTagValue(record.value->kind, tag, value);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(allowed, {literalText("태그 값이 모델의 태그 정의에 맞지 않음: "), record.value->id.view()});
    }
    return write([&](uint32_t sequence) { return _w.items.writer().setTag(item, tag, value, sequence); });
}

Result<RelationRecord, KiottySpecStatus> Editor::relationOf(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    using RelationResult = Result<RelationRecord, KiottySpecStatus>;
    const auto from = resolve(from_id);
    if (!from) { return RelationResult::failure(from.code); }
    const auto to = resolve(to_id);
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

KiottySpecStatus Editor::addRelation(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    if (_w.relations.contains(record.value)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 있는 관계")}); }
    return write([&](uint32_t sequence) { return _w.relations.add(record.value, sequence); });
}

KiottySpecStatus Editor::removeRelation(Span<const char> from_id, uint32_t relation, Span<const char> to_id)
{
    const auto record = relationOf(from_id, relation, to_id);
    if (!record) { return record.code; }
    if (!_w.relations.contains(record.value)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 관계")}); }
    return write([&](uint32_t sequence) { return _w.relations.remove(record.value, sequence); });
}

KiottySpecStatus Editor::writeDecision(ItemKind kind, Span<const char> text)
{
    if (!entity::isValidKind(static_cast<uint32_t>(kind)) || !entity::hasDecision(kind)) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("결정 기록이 없는 Kind")});
    }
    const KiottySpecStatus checked = checkText(text, MAX_DECISION_BYTES, "결정 기록");
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    return write([&](uint32_t sequence) { return _w.decisions.write(kind, text, sequence); });
}

}
