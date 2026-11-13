#include "implementation/domain/usecases/rule_sync/kiotty_rule_compare.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using StateResult = Result<const entity::ItemState&, KiottySpecStatus>;
using LabelResult = Result<const entity::ItemLabel&, KiottySpecStatus>;

constexpr int LOCAL_FIRST = -1;
constexpr int REMOTE_FIRST = 1;

uint32_t compareLabels(RuleSide local, RuleSide remote);
bool hasSameTags(RuleSide local, RuleSide remote);
uint32_t toBits(KiottySpecChangedField field);

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
    const bool is_local_used = compared <= 0;
    const bool is_remote_used = compared >= 0;
    if (is_local_used) { next.local = skipToRule(pair.local, order.local + 1); }
    if (is_remote_used) { next.remote = skipToRule(pair.remote, order.remote + 1); }
    return next;
}

uint32_t compareRules(RuleSide local, RuleSide remote)
{
    const bool is_same_parent = parentIdOf(local) == parentIdOf(remote);
    const bool is_same_tags = hasSameTags(local, remote);
    const std::string_view before = viewOf(local.workspace.items.reader().bodyOf(local.handle));
    const std::string_view after = viewOf(remote.workspace.items.reader().bodyOf(remote.handle));
    const bool is_same_body = before == after;
    uint32_t fields = compareLabels(local, remote);
    fields |= is_same_parent ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_PARENT);
    fields |= is_same_tags ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_TAGS);
    fields |= is_same_body ? 0u : toBits(KIOTTY_SPEC_CHANGED_FIELD_BODY);
    return fields;
}

std::string_view parentIdOf(RuleSide side)
{
    const StateResult state = side.workspace.items.reader().get(side.handle);
    if (!state) { return std::string_view(); }
    const ItemHandle parent = (*state.value).parent;
    const bool has_parent = parent.isValid();
    if (!has_parent) { return std::string_view(); }
    return idOf(RuleSide {side.workspace, parent});
}

std::string_view idOf(RuleSide side)
{
    const LabelResult label = side.workspace.items.reader().label(side.handle);
    return label ? std::string_view((*label.value).id) : std::string_view();
}

size_t skipToRule(const Workspace& workspace, size_t position)
{
    const size_t count = workspace.items.slots().count();
    for (size_t current = position; current < count; ++current) {
        const StateResult state = workspace.items.reader().get(ruleOf(workspace, current));
        const bool is_rule = state && (*state.value).kind == ItemKind::RULE;
        if (is_rule) { return current; }
    }
    return count;
}

ItemHandle ruleOf(const Workspace& workspace, size_t position)
{
    const size_t count = workspace.items.slots().count();
    const bool is_inside = position < count;
    if (!is_inside) { return entity::INVALID_ITEM_HANDLE; }
    const uint32_t slot = workspace.items.slots().readIdOrder(position);
    return workspace.items.slots().handleAt(slot);
}

namespace {

uint32_t compareLabels(RuleSide local, RuleSide remote)
{
    const LabelResult left = local.workspace.items.reader().label(local.handle);
    const LabelResult right = remote.workspace.items.reader().label(remote.handle);
    const bool has_labels = left && right;
    if (!has_labels) { return 0; }
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
        const bool is_same = before == after;
        if (!is_same) { return false; }
    }
    return true;
}

uint32_t toBits(KiottySpecChangedField field) { return static_cast<uint32_t>(field); }

}

}
