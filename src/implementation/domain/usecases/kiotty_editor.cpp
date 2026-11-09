#include "implementation/domain/usecases/kiotty_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/domain/entity/kiotty_item_id.h"
#include "implementation/domain/usecases/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;

}

KiottySpecStatus Editor::checkText(Span<const char> text, size_t limit, const char* field)
{
    if (text.length() > limit) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {boundedText(field, 32), literalText(" 이 상한을 넘음")});
    }
    if (!isValidUtf8(text)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_UTF8, {boundedText(field, 32), literalText(" 이 UTF-8 이 아님")}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Editor::markNeedsChange(ItemHandle item, uint32_t sequence)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return record.code; }
    const ItemKind kind = record.value->kind;
    if (kind != ItemKind::DESIGN && kind != ItemKind::UNIT_TEST_PLAN) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto state = _w.catalog.finder().findTag(kind, literalText("구현 상태"));
    if (!state) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto done = _w.catalog.finder().findTagBit(kind, state.value, literalText("구현 완료"));
    const auto changed = _w.catalog.finder().findTagBit(kind, state.value, literalText("변경 필요"));
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
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return IdResult::failure(scope.finish(sequence.code)); }
    const IdResult created = insertItem(kind, title, summary, sequence.value);
    status = scope.finish(created.code);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return created;
}

// 새 ID 를 받아 항목을 만든다.
Result<IdText, KiottySpecStatus> Editor::insertItem(ItemKind kind, Span<const char> title, Span<const char> summary, uint32_t sequence)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    const auto id = _w.meta.nextId(_w.catalog.model(kind).id_prefix.view());
    if (!id) { return IdResult::failure(id.code); }
    ItemRecord record {};
    record.parent = entity::INVALID_ITEM_HANDLE;
    record.kind = kind;
    record.id = id.value;
    record.title.assign(title);
    record.summary.assign(summary);
    const auto handle = _w.items.writer().create(record, sequence);
    if (!handle) { return IdResult::failure(handle.code); }
    return IdResult::success(id.value);
}

KiottySpecStatus Editor::updateItem(Span<const char> id, Span<const char> title, Span<const char> summary)
{
    const auto item = _w.resolve(id);
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
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.items.writer().update(item.value, record, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = markNeedsChange(item.value, sequence.value); }
    return scope.finish(status);
}

KiottySpecStatus Editor::setParent(Span<const char> id, Span<const char> parent_id)
{
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    ItemRecord record = *_w.items.reader().get(item.value).value;
    record.parent = entity::INVALID_ITEM_HANDLE;
    if (parent_id.length() > 0) {
        const auto parent = _w.resolve(parent_id);
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
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_w.items.writer().update(item.value, record, sequence.value));
}

KiottySpecStatus Editor::removeItem(Span<const char> id)
{
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    for (size_t index = 0; index < _w.items.reader().capacity(); ++index) {
        const auto child = _w.items.reader().get(_w.items.reader().handleAt(index));
        if (child && child.value->parent.index == item.value.index && child.value->parent.isValid()) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {literalText("하위 항목이 있어 지울 수 없음: "), child.value->id.view()});
        }
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.relations.removeAllOf(item.value, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _w.items.writer().remove(item.value, sequence.value); }
    return scope.finish(status);
}

KiottySpecStatus Editor::writeBody(Span<const char> id, Span<const char> body)
{
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    const KiottySpecStatus checked = checkText(body, entity::MAX_BODY_LENGTH, "본문");
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.items.writer().writeBody(item.value, body, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = markNeedsChange(item.value, sequence.value); }
    return scope.finish(status);
}

KiottySpecStatus Editor::setTag(ItemHandle item, uint32_t tag, uint64_t value)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return _w.failure.raise(record.code, {literalText("항목 핸들이 무효임")}); }
    const KiottySpecStatus allowed = _w.catalog.checkTagValue(record.value->kind, tag, value);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(allowed, {literalText("태그 값이 모델의 태그 정의에 맞지 않음: "), record.value->id.view()});
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_w.items.writer().setTag(item, tag, value, sequence.value));
}

KiottySpecStatus Editor::writeDecision(ItemKind kind, Span<const char> text)
{
    if (!entity::isValidKind(static_cast<uint32_t>(kind)) || !entity::hasDecision(kind)) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("결정 기록이 없는 Kind")});
    }
    const KiottySpecStatus checked = checkText(text, MAX_DECISION_BYTES, "결정 기록");
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) { return checked; }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_w.decisions.write(kind, text, sequence.value));
}

}
