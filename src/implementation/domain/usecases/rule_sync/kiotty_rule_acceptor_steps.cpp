#include "implementation/domain/usecases/rule_sync/kiotty_rule_acceptor.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::RelationRecord;

constexpr uint32_t MISSING_RELATION = UINT32_MAX;

bool isChange(const RuleChange& change, KiottySpecChange kind);
bool isCopied(const RuleChange& change);
size_t markOnce(Span<ItemHandle> marks, size_t count, ItemHandle item);

}

KiottySpecStatus RuleAcceptor::copyRules(Workspace& remote, uint32_t sequence)
{
    for (size_t index = 0; index < _change_count; ++index) {
        RuleChange& change = _changes[index];
        if (!isCopied(change)) { continue; }
        const KiottySpecStatus copied = copyRule(RuleSide {remote, change.remote}, change, sequence);
        if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return copied; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::copyRule(RuleSide remote, RuleChange& change, uint32_t sequence)
{
    const auto label = remote.workspace.items.reader().label(remote.handle);
    if (!label) { return label.code; }
    ItemWriter writer = _local.items.writer();
    if (isChange(change, KIOTTY_SPEC_CHANGE_ADDED)) {
        entity::ItemState state {};
        state.parent = entity::INVALID_ITEM_HANDLE;
        state.kind = ItemKind::RULE;
        const auto created = writer.create(*label.value, state, sequence);
        if (!created) { return created.code; }
        change.local = created.value;
        return copyContent(remote, change.local, sequence);
    }
    const auto current = _local.items.reader().get(change.local);
    if (!current) { return current.code; }
    const ItemHandle parent = (*current.value).parent;
    const KiottySpecStatus updated = writer.update(change.local, *label.value, parent, sequence);
    if (updated != KIOTTY_SPEC_STATUS_SUCCESS) { return updated; }
    return copyContent(remote, change.local, sequence);
}

KiottySpecStatus RuleAcceptor::copyContent(RuleSide remote, ItemHandle local, uint32_t sequence)
{
    ItemWriter writer = _local.items.writer();
    const Span<const char> body = remote.workspace.items.reader().body(remote.handle);
    KiottySpecStatus status = writer.writeBody(local, body, sequence);
    const size_t tag_count = _local.catalog.model(ItemKind::RULE).tags.length;
    for (uint32_t tag = 0; tag < tag_count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++tag) {
        const uint64_t before = _local.items.tags().value(local, tag);
        const uint64_t after = remote.workspace.items.tags().value(remote.handle, tag);
        if (before == after) { continue; }
        status = writer.setTag(local, tag, after, sequence);
    }
    return status;
}

KiottySpecStatus RuleAcceptor::linkParents(Workspace& remote, uint32_t sequence)
{
    for (size_t index = 0; index < _change_count; ++index) {
        const RuleChange& change = _changes[index];
        if (!isCopied(change)) { continue; }
        const std::string_view parent_id = parentIdOf(RuleSide {remote, change.remote});
        const auto found = _local.items.reader().find(spanOf(parent_id));
        const ItemHandle parent = found ? found.value : entity::INVALID_ITEM_HANDLE;
        const auto state = _local.items.reader().get(change.local);
        const auto label = _local.items.reader().label(change.local);
        if (!state || !label) { return KIOTTY_SPEC_STATUS_INVALID_HANDLE; }
        const bool is_same = (*state.value).parent.index == parent.index &&
                             (*state.value).parent.isValid() == parent.isValid();
        if (is_same) { continue; }
        const KiottySpecStatus updated =
            _local.items.writer().update(change.local, *label.value, parent, sequence);
        if (updated != KIOTTY_SPEC_STATUS_SUCCESS) { return updated; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::linkRequest(ItemHandle request, uint32_t sequence)
{
    const uint32_t relation = relationOf(ItemKind::CHANGE_REQUEST, AFFECTED_RULE_RELATION);
    for (size_t index = 0; index < _change_count; ++index) {
        const RuleChange& change = _changes[index];
        if (!isCopied(change)) { continue; }
        const RelationRecord record {request, change.local, ItemKind::CHANGE_REQUEST, relation};
        const KiottySpecStatus added = _local.relations.writer().add(record, sequence);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::linkPrivateRules(ItemHandle request, uint32_t sequence)
{
    const uint32_t replaced = relationOf(ItemKind::PRIVATE_RULE, REPLACED_RULE_RELATION);
    const uint32_t affected =
        relationOf(ItemKind::CHANGE_REQUEST, AFFECTED_PRIVATE_RULE_RELATION);
    const Span<ItemHandle> marks = viewVector(_marks);
    size_t count = 0;
    for (const RelationRecord& relation : _local.relations.all()) {
        const bool is_replacing = relation.model == ItemKind::PRIVATE_RULE &&
                                  relation.relation == replaced && isChangedTarget(relation.to);
        if (is_replacing) { count = markOnce(marks, count, relation.from); }
    }
    for (size_t index = 0; index < count; ++index) {
        const RelationRecord record {request, marks[index], ItemKind::CHANGE_REQUEST, affected};
        const KiottySpecStatus added = _local.relations.writer().add(record, sequence);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::removeResults(uint32_t sequence)
{
    const ItemKind result = ItemKind::CODE_INSPECTION_RESULT;
    const uint32_t inspected = relationOf(result, CODE_INSPECTION_RULE_RELATION);
    const Span<ItemHandle> marks = viewVector(_marks);
    size_t count = 0;
    for (const RelationRecord& relation : _local.relations.all()) {
        const bool is_stale = relation.model == result && relation.relation == inspected &&
                              isChangedTarget(relation.to);
        if (is_stale) { count = markOnce(marks, count, relation.from); }
    }
    for (size_t index = 0; index < count; ++index) {
        KiottySpecStatus status = _local.relations.writer().removeAllOf(marks[index], sequence);
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            status = _local.items.writer().remove(marks[index], sequence);
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RuleAcceptor::removeRules(uint32_t sequence)
{
    for (size_t index = 0; index < _change_count; ++index) {
        const RuleChange& change = _changes[index];
        if (!isChange(change, KIOTTY_SPEC_CHANGE_REMOVED)) { continue; }
        KiottySpecStatus status = _local.relations.writer().removeAllOf(change.local, sequence);
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            status = _local.items.writer().remove(change.local, sequence);
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

bool RuleAcceptor::isChangedTarget(ItemHandle rule) const
{
    for (size_t index = 0; index < _change_count; ++index) {
        const RuleChange& change = _changes[index];
        const bool is_target = !isChange(change, KIOTTY_SPEC_CHANGE_ADDED) &&
                               change.local.index == rule.index;
        if (is_target) { return true; }
    }
    return false;
}

uint32_t RuleAcceptor::relationOf(ItemKind kind, std::string_view name) const
{
    const auto found = _local.catalog.finder().findRelation(kind, spanOf(name));
    return found ? found.value : MISSING_RELATION;
}

namespace {

bool isChange(const RuleChange& change, KiottySpecChange kind)
{
    return change.change == static_cast<uint32_t>(kind);
}

bool isCopied(const RuleChange& change)
{
    const bool is_added = isChange(change, KIOTTY_SPEC_CHANGE_ADDED);
    const bool is_modified = isChange(change, KIOTTY_SPEC_CHANGE_MODIFIED);
    return is_added || is_modified;
}

size_t markOnce(Span<ItemHandle> marks, size_t count, ItemHandle item)
{
    for (size_t index = 0; index < count; ++index) {
        if (marks[index].index == item.index) { return count; }
    }
    if (count >= marks.length()) { return count; }
    marks[count] = item;
    return count + 1;
}

}

}
