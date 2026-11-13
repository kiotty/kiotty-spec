#include "implementation/domain/usecases/rule_sync/kiotty_rule_compare.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;

constexpr int LOCAL_FIRST = -1;
constexpr int REMOTE_FIRST = 1;

uint32_t compareLabels(RuleSide local, RuleSide remote);
bool hasSameTags(RuleSide local, RuleSide remote);
bool hasSameText(Span<const char> left, Span<const char> right);
uint32_t toBits(KiottySpecChangedField field);

}

size_t skipToRule(Workspace& workspace, size_t position)
{
    const size_t count = workspace.items.slots().count();
    size_t current = position;
    while (current < count) {
        const auto state = workspace.items.reader().get(ruleAt(workspace, current));
        const bool is_rule = state && (*state.value).kind == ItemKind::RULE;
        if (is_rule) { return current; }
        ++current;
    }
    return count;
}

ItemHandle ruleAt(Workspace& workspace, size_t position)
{
    const size_t count = workspace.items.slots().count();
    if (position >= count) { return entity::INVALID_ITEM_HANDLE; }
    const uint32_t slot = workspace.items.slots().readIdOrder(position);
    return workspace.items.slots().handleAt(slot);
}

std::string_view idOf(RuleSide side)
{
    const auto label = side.workspace.items.reader().label(side.handle);
    return label ? std::string_view((*label.value).id) : std::string_view();
}

std::string_view parentIdOf(RuleSide side)
{
    const auto state = side.workspace.items.reader().get(side.handle);
    if (!state) { return std::string_view(); }
    const ItemHandle parent = (*state.value).parent;
    if (!parent.isValid()) { return std::string_view(); }
    return idOf(RuleSide {side.workspace, parent});
}

uint32_t compareRules(RuleSide local, RuleSide remote)
{
    const bool is_same_parent = parentIdOf(local) == parentIdOf(remote);
    const bool is_same_tags = hasSameTags(local, remote);
    const bool is_same_body = hasSameText(local.workspace.items.reader().body(local.handle),
                                          remote.workspace.items.reader().body(remote.handle));
    uint32_t fields = compareLabels(local, remote);
    fields |= is_same_parent ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_PARENT);
    fields |= is_same_tags ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_TAGS);
    fields |= is_same_body ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_BODY);
    return fields;
}

int compareOrder(RuleSide local, RuleSide remote, RuleOrder order)
{
    const bool is_local_done = order.local == local.workspace.items.slots().count();
    if (is_local_done) { return REMOTE_FIRST; }
    const bool is_remote_done = order.remote == remote.workspace.items.slots().count();
    if (is_remote_done) { return LOCAL_FIRST; }
    return idOf(local).compare(idOf(remote));
}

RuleOrder advanceOrder(RulePair pair, RuleOrder order, int compared)
{
    RuleOrder next = order;
    if (compared <= 0) { next.local = skipToRule(pair.local, order.local + 1); }
    if (compared >= 0) { next.remote = skipToRule(pair.remote, order.remote + 1); }
    return next;
}

namespace {

uint32_t compareLabels(RuleSide local, RuleSide remote)
{
    const auto left = local.workspace.items.reader().label(local.handle);
    const auto right = remote.workspace.items.reader().label(remote.handle);
    if (!left || !right) { return 0; }
    const entity::ItemLabel& before = *left.value;
    const entity::ItemLabel& after = *right.value;
    const bool is_same_title = std::string_view(before.title) == std::string_view(after.title);
    const bool is_same_summary =
        std::string_view(before.summary) == std::string_view(after.summary);
    const uint32_t title = is_same_title ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_TITLE);
    const uint32_t summary = is_same_summary ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_SUMMARY);
    return title | summary;
}

bool hasSameTags(RuleSide local, RuleSide remote)
{
    const entity::Model& model = local.workspace.catalog.model(ItemKind::RULE);
    const size_t tag_count = model.tags.length;
    for (uint32_t tag = 0; tag < tag_count; ++tag) {
        const uint64_t before = local.workspace.items.tags().value(local.handle, tag);
        const uint64_t after = remote.workspace.items.tags().value(remote.handle, tag);
        if (before != after) { return false; }
    }
    return true;
}

bool hasSameText(Span<const char> left, Span<const char> right)
{
    return std::string_view(left.data(), left.length()) ==
           std::string_view(right.data(), right.length());
}

uint32_t toBits(KiottySpecChangedField field) { return static_cast<uint32_t>(field); }

}

}
