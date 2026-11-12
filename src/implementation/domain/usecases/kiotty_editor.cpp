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

ItemRecord makeDraftItem(ItemKind kind, Span<const char> title, Span<const char> summary);
bool isInParentChain(const ItemReader& reader, ItemHandle start, ItemHandle item);

}

Result<IdText, KiottySpecStatus> Editor::createItem(ItemKind kind, Span<const char> id,
                                                    Span<const char> title,
                                                    Span<const char> summary)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    const bool is_valid_kind = entity::isValidKind(static_cast<uint32_t>(kind));
    if (!is_valid_kind) {
        return IdResult::failure(_workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                          KIOTTY_SPEC_SUBJECT_KIND));
    }
    const size_t title_length = title.length();
    if (title_length == 0) {
        return IdResult::failure(_workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                          KIOTTY_SPEC_SUBJECT_TITLE));
    }
    KiottySpecStatus status = id.length() > 0 ? checkId(id) : KIOTTY_SPEC_STATUS_SUCCESS;
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return IdResult::failure(scope.finish(sequence.code)); }
    const ItemRecord draft = makeDraftItem(kind, title, summary);
    const IdResult created = insertItem(draft, id, sequence.value);
    status = scope.finish(created.code);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return created;
}

Result<IdText, KiottySpecStatus> Editor::insertItem(const ItemRecord& draft, Span<const char> given,
                                                    uint32_t sequence)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    Result<IdText, KiottySpecStatus> id = IdResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    const size_t given_length = given.length();
    if (given_length > 0) {
        IdText text {};
        text.assign(given);
        id = IdResult::success(text);
    } else {
        id = _workspace.items.history().nextId(
            _workspace.catalog.model(draft.kind).id_prefix.view());
    }
    if (!id) { return IdResult::failure(id.code); }
    ItemRecord record = draft;
    record.id = id.value;
    const auto handle = _workspace.items.writer().create(record, sequence);
    if (!handle) { return IdResult::failure(handle.code); }
    return IdResult::success(id.value);
}

KiottySpecStatus Editor::renameItem(Span<const char> id, Span<const char> new_id)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    const KiottySpecStatus valid = checkId(new_id);
    if (valid != KIOTTY_SPEC_STATUS_SUCCESS) { return valid; }
    IdText next {};
    next.assign(new_id);
    return _workspace.finish(_workspace.items.writer().rename(item.value, next));
}

KiottySpecStatus Editor::checkId(Span<const char> id)
{
    const KiottySpecStatus valid = entity::validateItemId(id);
    if (valid == KIOTTY_SPEC_STATUS_SUCCESS) { return valid; }
    return _workspace.failure.raise(valid, KIOTTY_SPEC_SUBJECT_ID, id);
}

KiottySpecStatus Editor::updateItem(Span<const char> id, Span<const char> title,
                                    Span<const char> summary)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    ItemRecord record = *_workspace.items.reader().get(item.value).value;
    const bool has_title = title.data() != nullptr;
    if (has_title) {
        const size_t title_length = title.length();
        if (title_length == 0) {
            return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                            KIOTTY_SPEC_SUBJECT_TITLE);
        }
        record.title.assign(title);
    }
    const bool has_summary = summary.data() != nullptr;
    if (has_summary) {
        record.summary.assign(summary);
    }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _workspace.items.writer().update(item.value, record, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus Editor::setParent(Span<const char> id, Span<const char> parent_id)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    const Result<ItemHandle, KiottySpecStatus> parent = resolveParent(item.value, id, parent_id);
    if (!parent) { return parent.code; }
    ItemRecord record = *_workspace.items.reader().get(item.value).value;
    record.parent = parent.value;
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.items.writer().update(item.value, record, sequence.value));
}

Result<ItemHandle, KiottySpecStatus> Editor::resolveParent(ItemHandle item, Span<const char> id,
                                                           Span<const char> parent_id)
{
    using HandleResult = Result<ItemHandle, KiottySpecStatus>;
    const size_t parent_id_length = parent_id.length();
    if (parent_id_length == 0) { return HandleResult::success(entity::INVALID_ITEM_HANDLE); }
    const HandleResult parent = _workspace.resolve(parent_id);
    if (!parent) { return parent; }
    const bool is_cycle = isInParentChain(_workspace.items.reader(), parent.value, item);
    if (is_cycle) {
        return HandleResult::failure(_workspace.failure.raise(
            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_PARENT, id));
    }
    return parent;
}

KiottySpecStatus Editor::removeItem(Span<const char> id)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    const size_t capacity = _workspace.items.reader().capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = _workspace.items.reader().handleAt(index);
        const auto child = _workspace.items.reader().get(handle);
        const bool is_child = child && child.value->parent.index == item.value.index;
        if (!is_child) { continue; }
        const bool has_parent = child.value->parent.isValid();
        if (has_parent) {
            return _workspace.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION,
                                            KIOTTY_SPEC_SUBJECT_CHILD, child.value->id.view());
        }
    }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _workspace.relations.removeAllOf(item.value, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _workspace.items.writer().remove(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus Editor::writeBody(Span<const char> id, Span<const char> body)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _workspace.items.writer().writeBody(item.value, body, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus Editor::markNeedsChange(ItemHandle item, uint32_t sequence)
{
    const auto record = _workspace.items.reader().get(item);
    if (!record) { return record.code; }
    const ItemKind kind = record.value->kind;
    if (kind != ItemKind::DESIGN && kind != ItemKind::UNIT_TEST_PLAN) {
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    using IndexResult = Result<uint32_t, KiottySpecStatus>;
    const IndexResult state =
        _workspace.catalog.finder().findTag(kind, spanOf(IMPLEMENTATION_STATE_TAG));
    if (!state) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const IndexResult done =
        _workspace.catalog.finder().findTagBit(kind, state.value, spanOf(IMPLEMENTED_BIT));
    const IndexResult changed =
        _workspace.catalog.finder().findTagBit(kind, state.value, spanOf(NEEDS_CHANGE_BIT));
    const bool has_state_bits = done && changed;
    if (!has_state_bits) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    const uint64_t current = _workspace.items.tags().value(item, state.value);
    if (current != (uint64_t { 1 } << done.value)) {
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    return _workspace.items.writer().setTag(item, state.value, uint64_t{1} << changed.value,
                                            sequence);
}

KiottySpecStatus Editor::setTag(ItemHandle item, uint32_t tag, uint64_t value)
{
    const auto record = _workspace.items.reader().get(item);
    if (!record) { return _workspace.failure.raise(record.code, KIOTTY_SPEC_SUBJECT_ITEM); }
    const size_t tag_count = _workspace.catalog.model(record.value->kind).tags.length();
    if (tag >= tag_count) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_TAG, record.value->id.view());
    }
    const KiottySpecStatus allowed =
        _workspace.catalog.checkTagValue(record.value->kind, tag, value);
    if (allowed != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _workspace.failure.raise(allowed, KIOTTY_SPEC_SUBJECT_TAG_VALUE,
                                        record.value->id.view());
    }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.items.writer().setTag(item, tag, value, sequence.value));
}

KiottySpecStatus Editor::writeDecision(ItemKind kind, Span<const char> text)
{
    const bool is_valid_kind = entity::isValidKind(static_cast<uint32_t>(kind));
    const bool has_decision = is_valid_kind && entity::hasDecision(kind);
    if (!has_decision) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_KIND);
    }
    WriteScope scope(_workspace, _lifecycle);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.decisions.write(kind, text, sequence.value));
}

namespace {

ItemRecord makeDraftItem(ItemKind kind, Span<const char> title, Span<const char> summary)
{
    ItemRecord record {};
    record.parent = entity::INVALID_ITEM_HANDLE;
    record.kind = kind;
    record.title.assign(title);
    record.summary.assign(summary);
    return record;
}

bool isInParentChain(const ItemReader& reader, ItemHandle start, ItemHandle item)
{
    ItemHandle probe = start;
    bool is_valid = probe.isValid();
    while (is_valid) {
        if (probe.index == item.index) { return true; }
        const auto above = reader.get(probe);
        if (!above) { return false; }
        probe = above.value->parent;
        is_valid = probe.isValid();
    }
    return false;
}

}

}
