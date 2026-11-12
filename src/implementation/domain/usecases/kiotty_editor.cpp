#include "implementation/domain/usecases/kiotty_editor.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/domain/usecases/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemLabel;
using entity::ItemState;

ItemLabel makeDraftLabel(Span<const char> title, Span<const char> summary);
bool isInParentChain(const ItemReader& reader, ItemHandle start, ItemHandle item);

}

Result<IdText, KiottySpecStatus>
StructureEditor::createItem(ItemKind kind, Span<const char> id, Span<const char> title,
                            Span<const char> summary)
{
    using IdResult = Result<IdText, KiottySpecStatus>;
    const size_t title_length = title.length();
    if (title_length == 0) {
        return IdResult::failure(_workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                                          KIOTTY_SPEC_SUBJECT_TITLE));
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return IdResult::failure(scope.finish(sequence.code)); }
    const ItemLabel draft = makeDraftLabel(title, summary);
    const IdResult created = insertItem(kind, draft, id, sequence.value);
    const KiottySpecStatus status = scope.finish(created.code);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return created;
}

Result<IdText, KiottySpecStatus>
StructureEditor::insertItem(ItemKind kind, const ItemLabel& draft, Span<const char> given,
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
            _workspace.catalog.model(kind).id_prefix.view());
    }
    if (!id) { return IdResult::failure(id.code); }
    ItemLabel label = draft;
    label.id = id.value;
    ItemState state {};
    state.parent = entity::INVALID_ITEM_HANDLE;
    state.kind = kind;
    const auto handle = _workspace.items.writer().create(label, state, sequence);
    if (!handle) { return IdResult::failure(handle.code); }
    return IdResult::success(id.value);
}

KiottySpecStatus StructureEditor::renameItem(Span<const char> id, Span<const char> new_id)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    IdText next {};
    next.assign(new_id);
    return _workspace.finish(_workspace.items.writer().rename(item.value, next));
}

KiottySpecStatus StructureEditor::setParent(Span<const char> id, Span<const char> parent_id)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    const Result<ItemHandle, KiottySpecStatus> parent = resolveParent(item.value, id, parent_id);
    if (!parent) { return parent.code; }
    const ItemLabel label = *_workspace.items.reader().label(item.value).value;
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    const KiottySpecStatus updated =
        _workspace.items.writer().update(item.value, label, parent.value, sequence.value);
    return scope.finish(updated);
}

Result<ItemHandle, KiottySpecStatus>
StructureEditor::resolveParent(ItemHandle item, Span<const char> id, Span<const char> parent_id)
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

KiottySpecStatus StructureEditor::removeItem(Span<const char> id)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    const KiottySpecStatus childless = checkNoChildren(item.value);
    if (childless != KIOTTY_SPEC_STATUS_SUCCESS) { return childless; }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status =
        _workspace.relations.writer().removeAllOf(item.value, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _workspace.items.writer().remove(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus StructureEditor::checkNoChildren(ItemHandle item)
{
    const ItemSlots slots = _workspace.items.slots();
    const size_t capacity = slots.capacity();
    for (size_t index = 0; index < capacity; ++index) {
        const ItemHandle handle = slots.handleAt(index);
        const auto child = _workspace.items.reader().get(handle);
        if (!child) { continue; }
        const ItemState& state = *child.value;
        const bool is_child = state.parent.index == item.index;
        const bool has_parent = state.parent.isValid();
        if (is_child && has_parent) {
            const ItemLabel& label = *_workspace.items.reader().label(handle).value;
            return _workspace.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION,
                                            KIOTTY_SPEC_SUBJECT_CHILD, label.id.view());
        }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ContentEditor::updateItem(Span<const char> id, OptionalText title,
                                           OptionalText summary)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    ItemLabel label = *_workspace.items.reader().label(item.value).value;
    const ItemHandle parent = (*_workspace.items.reader().get(item.value).value).parent;
    const bool has_title = title.has_value();
    if (has_title) {
        const size_t title_length = (*title).length();
        if (title_length == 0) {
            return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                            KIOTTY_SPEC_SUBJECT_TITLE);
        }
        label.title.assign(*title);
    }
    const bool has_summary = summary.has_value();
    if (has_summary) {
        label.summary.assign(*summary);
    }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status =
        _workspace.items.writer().update(item.value, label, parent, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus ContentEditor::writeBody(Span<const char> id, Span<const char> body)
{
    const auto item = _workspace.resolve(id);
    if (!item) { return item.code; }
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    KiottySpecStatus status = _workspace.items.writer().writeBody(item.value, body, sequence.value);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = markNeedsChange(item.value, sequence.value);
    }
    return scope.finish(status);
}

KiottySpecStatus ContentEditor::markNeedsChange(ItemHandle item, uint32_t sequence)
{
    const auto record = _workspace.items.reader().get(item);
    if (!record) { return record.code; }
    const ItemKind kind = (*record.value).kind;
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

KiottySpecStatus ContentEditor::setTag(const entity::TagRecord& tag)
{
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(
        _workspace.items.writer().setTag(tag.item, tag.tag, tag.value, sequence.value));
}

KiottySpecStatus ContentEditor::writeDecision(ItemKind kind, Span<const char> text)
{
    WriteScope scope(_workspace);
    const Result<uint32_t, KiottySpecStatus> sequence = scope.sequence();
    if (!sequence) { return scope.finish(sequence.code); }
    return scope.finish(_workspace.decisions.write(kind, text, sequence.value));
}

namespace {

ItemLabel makeDraftLabel(Span<const char> title, Span<const char> summary)
{
    ItemLabel label {};
    label.title.assign(title);
    label.summary.assign(summary);
    return label;
}

bool isInParentChain(const ItemReader& reader, ItemHandle start, ItemHandle item)
{
    ItemHandle probe = start;
    bool is_valid = probe.isValid();
    while (is_valid) {
        if (probe.index == item.index) { return true; }
        const auto above = reader.get(probe);
        if (!above) { return false; }
        probe = (*above.value).parent;
        is_valid = probe.isValid();
    }
    return false;
}

}

}
