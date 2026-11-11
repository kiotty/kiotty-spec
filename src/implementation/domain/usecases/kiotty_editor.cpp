#include "implementation/domain/usecases/kiotty_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/entity/kiotty_item_id.h"
#include "implementation/domain/usecases/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;

}

KiottySpecStatus Editor::markNeedsChange(ItemHandle item, uint32_t sequence)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return record.code; }
    const ItemKind kind = record.value->kind;
    if (kind != ItemKind::DESIGN && kind != ItemKind::UNIT_TEST_PLAN) {
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    const auto state = _w.catalog.finder().findTag(kind, spanOf(IMPLEMENTATION_STATE_TAG));
    if (!state) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const auto done = _w.catalog.finder().findTagBit(kind, state.value, spanOf(IMPLEMENTED_BIT));
    const auto changed =
        _w.catalog.finder().findTagBit(kind, state.value, spanOf(NEEDS_CHANGE_BIT));
    if (!done || !changed) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (_w.items.tags().value(item, state.value) != (uint64_t { 1 } << done.value)) {
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    return _w.items.writer().setTag(item, state.value, uint64_t{1} << changed.value, sequence);
}

KiottySpecStatus Editor::checkId(Span<const char> id)
{
    const KiottySpecStatus valid = entity::validateItemId(id);
    if (valid == KIOTTY_SPEC_STATUS_SUCCESS) { return valid; }
    return _w.failure.raise(valid, KIOTTY_SPEC_SUBJECT_ID, id);
}

Result<IdText, KiottySpecStatus> Editor::createItem(ItemKind kind, Span<const char> id,
                                                    Span<const char> title,
                                                    Span<const char> summary)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    if (!entity::isValidKind(static_cast<uint32_t>(kind))) {
        return IdResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_KIND));
    }
    if (title.length() == 0) {
        return IdResult::failure(
            _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TITLE));
    }
    KiottySpecStatus status = id.length() > 0 ? checkId(id) : KIOTTY_SPEC_STATUS_SUCCESS;
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return IdResult::failure(scope.finish(sequence.code)); }
    const IdResult created = insertItem(kind, id, title, summary, sequence.value);
    status = scope.finish(created.code);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return created;
}

Result<IdText, KiottySpecStatus> Editor::insertItem(ItemKind kind, Span<const char> given,
                                                    Span<const char> title,
                                                    Span<const char> summary, uint32_t sequence)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    Result<IdText, KiottySpecStatus> id = IdResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    if (given.length() > 0) {
        IdText text {};
        text.assign(given);
        id = IdResult::success(text);
    } else {
        id = _w.items.history().nextId(_w.catalog.model(kind).id_prefix.view());
    }
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

KiottySpecStatus Editor::renameItem(Span<const char> id, Span<const char> new_id)
{
    const KiottySpecStatus fresh = _w.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    const KiottySpecStatus valid = checkId(new_id);
    if (valid != KIOTTY_SPEC_STATUS_SUCCESS) { return valid; }
    IdText next {};
    next.assign(new_id);
    return _w.finish(_w.items.writer().rename(item.value, next));
}

KiottySpecStatus Editor::updateItem(Span<const char> id, Span<const char> title,
                                    Span<const char> summary)
{
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    ItemRecord record = *_w.items.reader().get(item.value).value;
    if (title.data() != nullptr) {
        if (title.length() == 0) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TITLE);
        }
        record.title.assign(title);
    }
    if (summary.data() != nullptr) {
        record.summary.assign(summary);
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.items.writer().update(item.value, record, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
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
                return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_PARENT, id);
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
        if (child && child.value->parent.index == item.value.index &&
            child.value->parent.isValid()) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHILD,
                                    child.value->id.view());
        }
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.relations.removeAllOf(item.value, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _w.items.writer().remove(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus Editor::writeBody(Span<const char> id, Span<const char> body)
{
    const auto item = _w.resolve(id);
    if (!item) { return item.code; }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _w.items.writer().writeBody(item.value, body, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus Editor::setTag(ItemHandle item, uint32_t tag, uint64_t value)
{
    const auto record = _w.items.reader().get(item);
    if (!record) { return _w.failure.raise(record.code, KIOTTY_SPEC_SUBJECT_ITEM); }
    if (tag >= _w.catalog.model(record.value->kind).tags.length()) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_TAG,
                                record.value->id.view());
    }
    const KiottySpecStatus allowed = _w.catalog.checkTagValue(record.value->kind, tag, value);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(allowed, KIOTTY_SPEC_SUBJECT_TAG_VALUE, record.value->id.view());
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_w.items.writer().setTag(item, tag, value, sequence.value));
}

KiottySpecStatus Editor::writeDecision(ItemKind kind, Span<const char> text)
{
    if (!entity::isValidKind(static_cast<uint32_t>(kind)) || !entity::hasDecision(kind)) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_KIND);
    }
    WriteScope scope(_w, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_w.decisions.write(kind, text, sequence.value));
}

}
