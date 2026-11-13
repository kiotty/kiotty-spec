#include "implementation/domain/usecases/rule_sync/kiotty_rule_acceptor.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::RelationRecord;
using HandleResult = Result<ItemHandle, KiottySpecStatus>;
using IndexResult = Result<uint32_t, KiottySpecStatus>;
using LabelResult = Result<const entity::ItemLabel&, KiottySpecStatus>;
using StateResult = Result<const entity::ItemState&, KiottySpecStatus>;

constexpr uint32_t MISSING_RELATION = UINT32_MAX;

bool isCopied(const RuleChange& change);
bool isChange(const RuleChange& change, KiottySpecChange kind);
bool isSameHandle(ItemHandle left, ItemHandle right);
size_t markOnce(Span<ItemHandle> marks, size_t count, ItemHandle item);

}

KiottySpecStatus RuleAcceptor::copyRules(const Workspace& remote, uint32_t sequence)
{
    for (RuleChange& change : changes()) {
        const bool is_copied = isCopied(change);
        if (!is_copied) { continue; }
        const HandleResult copied = copyRule(RuleSide {remote, change.remote}, change, sequence);
        if (!copied) { return copied.code; }
        change.local = copied.value;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<ItemHandle, KiottySpecStatus> RuleAcceptor::copyRule(RuleSide remote,
                                                            const RuleChange& change,
                                                            uint32_t sequence)
{
    const LabelResult label = remote.workspace.items.reader().label(remote.handle);
    if (!label) { return HandleResult::failure(label.code); }
    ItemWriter writer = _local.items.writer();
    const bool is_added = isChange(change, KIOTTY_SPEC_CHANGE_ADDED);
    entity::ItemState state {};
    state.parent = entity::INVALID_ITEM_HANDLE;
    state.kind = ItemKind::RULE;
    const StateResult current = _local.items.reader().get(change.local);
    const ItemHandle parent = current ? (*current.value).parent : entity::INVALID_ITEM_HANDLE;
    const HandleResult written = is_added
        ? writer.create(*label.value, state, sequence)
        : HandleResult::success(change.local);
    if (!written) { return written; }
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    if (!is_added) { status = writer.update(change.local, *label.value, parent, sequence); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = copyContent(remote, written.value, sequence);
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return HandleResult::failure(status); }
    return written;
}

KiottySpecStatus RuleAcceptor::copyContent(RuleSide remote, ItemHandle local, uint32_t sequence)
{
    ItemWriter writer = _local.items.writer();
    const Span<const char> body = remote.workspace.items.reader().bodyOf(remote.handle);
    KiottySpecStatus status = writer.writeBody(local, body, sequence);
    const size_t tag_count = _local.catalog.model(ItemKind::RULE).tags.length;
    for (uint32_t tag = 0; tag < tag_count; ++tag) {
        const uint64_t before = _local.items.tags().value(local, tag);
        const uint64_t after = remote.workspace.items.tags().value(remote.handle, tag);
        const bool is_kept = before == after || status != KIOTTY_SPEC_STATUS_SUCCESS;
        if (is_kept) { continue; }
        status = writer.setTag(local, tag, after, sequence);
    }
    return status;
}

KiottySpecStatus RuleAcceptor::linkParents(const Workspace& remote, uint32_t sequence)
{
    for (const RuleChange& change : changes()) {
        const bool is_copied = isCopied(change);
        if (!is_copied) { continue; }
        const KiottySpecStatus linked =
            linkParent(RuleSide {remote, change.remote}, change.local, sequence);
        if (linked != KIOTTY_SPEC_STATUS_SUCCESS) { return linked; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::linkParent(RuleSide remote, ItemHandle local, uint32_t sequence)
{
    const HandleResult found = _local.items.reader().find(spanOf(parentIdOf(remote)));
    const ItemHandle parent = found ? found.value : entity::INVALID_ITEM_HANDLE;
    const StateResult state = _local.items.reader().get(local);
    const LabelResult label = _local.items.reader().label(local);
    const bool has_item = state && label;
    if (!has_item) { return KIOTTY_SPEC_STATUS_INVALID_HANDLE; }
    const bool is_same = isSameHandle((*state.value).parent, parent);
    if (is_same) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _local.items.writer().update(local, *label.value, parent, sequence);
}

KiottySpecStatus RuleAcceptor::linkRequest(ItemHandle request, uint32_t sequence)
{
    const uint32_t relation = findRelation(ItemKind::CHANGE_REQUEST, AFFECTED_RULE_RELATION);
    for (const RuleChange& change : changes()) {
        const bool is_copied = isCopied(change);
        if (!is_copied) { continue; }
        const RelationRecord record {request, change.local, ItemKind::CHANGE_REQUEST, relation};
        const KiottySpecStatus added = _local.relations.writer().add(record, sequence);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::linkPrivateRules(ItemHandle request, uint32_t sequence)
{
    const uint32_t replaced = findRelation(ItemKind::PRIVATE_RULE, REPLACED_RULE_RELATION);
    const uint32_t affected =
        findRelation(ItemKind::CHANGE_REQUEST, AFFECTED_PRIVATE_RULE_RELATION);
    const size_t count = markReplacing(replaced);
    const Span<ItemHandle> marks = viewVector(_marks).subspan(0, count).value;
    for (const ItemHandle rule : marks) {
        const RelationRecord record {request, rule, ItemKind::CHANGE_REQUEST, affected};
        const KiottySpecStatus added = _local.relations.writer().add(record, sequence);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::removeResults(uint32_t sequence)
{
    const uint32_t inspected =
        findRelation(ItemKind::CODE_INSPECTION_RESULT, CODE_INSPECTION_RULE_RELATION);
    const size_t count = markInspecting(inspected);
    const Span<ItemHandle> marks = viewVector(_marks).subspan(0, count).value;
    for (const ItemHandle result : marks) {
        KiottySpecStatus status = _local.relations.writer().removeAllOf(result, sequence);
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            status = _local.items.writer().remove(result, sequence);
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::removeRules(uint32_t sequence)
{
    for (const RuleChange& change : changes()) {
        const bool is_removed = isChange(change, KIOTTY_SPEC_CHANGE_REMOVED);
        if (!is_removed) { continue; }
        KiottySpecStatus status = _local.relations.writer().removeAllOf(change.local, sequence);
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            status = _local.items.writer().remove(change.local, sequence);
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

size_t RuleAcceptor::markReplacing(uint32_t relation)
{
    const Span<ItemHandle> marks = viewVector(_marks);
    size_t count = 0;
    for (const RelationRecord& record : _local.relations.all()) {
        const bool is_relation =
            record.model == ItemKind::PRIVATE_RULE && record.relation == relation;
        const bool is_marked = is_relation && isChangedTarget(record.to);
        if (is_marked) { count = markOnce(marks, count, record.from); }
    }
    return count;
}

size_t RuleAcceptor::markInspecting(uint32_t relation)
{
    const Span<ItemHandle> marks = viewVector(_marks);
    size_t count = 0;
    for (const RelationRecord& record : _local.relations.all()) {
        const bool is_relation =
            record.model == ItemKind::CODE_INSPECTION_RESULT && record.relation == relation;
        const bool is_marked = is_relation && isChangedTarget(record.to);
        if (is_marked) { count = markOnce(marks, count, record.from); }
    }
    return count;
}

bool RuleAcceptor::isChangedTarget(ItemHandle rule)
{
    for (const RuleChange& change : changes()) {
        const bool is_existing = isChange(change, KIOTTY_SPEC_CHANGE_ADDED) == false;
        const bool is_target = is_existing && isSameHandle(change.local, rule);
        if (is_target) { return true; }
    }
    return false;
}

uint32_t RuleAcceptor::findRelation(ItemKind kind, std::string_view name) const
{
    const IndexResult found = _local.catalog.finder().findRelation(kind, spanOf(name));
    return found ? found.value : MISSING_RELATION;
}

namespace {

bool isCopied(const RuleChange& change)
{
    const bool is_added = isChange(change, KIOTTY_SPEC_CHANGE_ADDED);
    const bool is_modified = isChange(change, KIOTTY_SPEC_CHANGE_MODIFIED);
    return is_added || is_modified;
}

bool isChange(const RuleChange& change, KiottySpecChange kind)
{
    return change.change == static_cast<uint32_t>(kind);
}

size_t markOnce(Span<ItemHandle> marks, size_t count, ItemHandle item)
{
    const Span<ItemHandle> used = marks.subspan(0, count).value;
    for (const ItemHandle marked : used) {
        const bool is_marked = isSameHandle(marked, item);
        if (is_marked) { return count; }
    }
    const Result<ItemHandle&, KiottySpecStatus> slot = marks.at(count);
    if (!slot) { return count; }
    *slot.value = item;
    return count + 1;
}

bool isSameHandle(ItemHandle left, ItemHandle right)
{
    const bool is_same_validity = left.isValid() == right.isValid();
    return is_same_validity && left.index == right.index;
}

}

}
