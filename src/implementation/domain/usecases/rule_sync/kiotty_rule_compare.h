#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

struct RuleSide {
    Workspace& workspace;
    entity::ItemHandle handle;
};

struct RulePair {
    Workspace& local;
    Workspace& remote;
};

struct RuleOrder {
    size_t local {0};
    size_t remote {0};
};

static_assert(sizeof(RuleSide) == 16, "unexpected padding");
static_assert(sizeof(RulePair) == 16, "unexpected padding");
static_assert(sizeof(RuleOrder) == 16, "unexpected padding");

size_t skipToRule(Workspace& workspace, size_t position);
entity::ItemHandle ruleAt(Workspace& workspace, size_t position);
std::string_view idOf(RuleSide side);
std::string_view parentIdOf(RuleSide side);
uint32_t compareRules(RuleSide local, RuleSide remote);
int compareOrder(RuleSide local, RuleSide remote, RuleOrder order);
RuleOrder advanceOrder(RulePair pair, RuleOrder order, int compared);

}
