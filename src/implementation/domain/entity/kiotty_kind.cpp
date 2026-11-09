#include "kiotty_kind.h"

namespace kiotty::spec::implementation::entity {

namespace {

constexpr std::string_view KIND_NAMES[ITEM_KIND_COUNT] = {
    "change_request",
    "requirement",
    "rule",
    "external_interface",
    "design",
    "unit_test_plan",
    "milestone",
    "unit_test_result",
    "system_test_plan",
    "system_test_result",
    "code_inspection_result",
    "e2e_test_plan",
    "e2e_test_result",
};

constexpr std::string_view STATE_NAMES[] = {"started", "running", "done"};

}

std::string_view kindName(ItemKind kind)
{
    const uint32_t value = static_cast<uint32_t>(kind);
    if (!isValidKind(value)) { return std::string_view(); }
    return KIND_NAMES[value - 1];
}

uint32_t parseKind(std::string_view name)
{
    for (uint32_t index = 0; index < ITEM_KIND_COUNT; ++index) {
        if (KIND_NAMES[index] == name) { return index + 1; }
    }
    return 0;
}

std::string_view stateName(CheckpointState state)
{
    const uint32_t value = static_cast<uint32_t>(state);
    if (value < 1 || value > 3) { return std::string_view(); }
    return STATE_NAMES[value - 1];
}

uint32_t parseState(std::string_view name)
{
    for (uint32_t index = 0; index < 3; ++index) {
        if (STATE_NAMES[index] == name) { return index + 1; }
    }
    return 0;
}

bool hasDecision(ItemKind kind)
{
    switch (kind) {
    case ItemKind::REQUIREMENT:
    case ItemKind::RULE:
    case ItemKind::EXTERNAL_INTERFACE:
    case ItemKind::DESIGN:
    case ItemKind::UNIT_TEST_PLAN:
    case ItemKind::MILESTONE: return true;
    case ItemKind::CHANGE_REQUEST:
    case ItemKind::UNIT_TEST_RESULT:
    case ItemKind::SYSTEM_TEST_PLAN:
    case ItemKind::SYSTEM_TEST_RESULT:
    case ItemKind::CODE_INSPECTION_RESULT:
    case ItemKind::END_TO_END_TEST_PLAN:
    case ItemKind::END_TO_END_TEST_RESULT: return false;
    }
    return false;
}

}
