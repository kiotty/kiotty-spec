#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation::entity {

enum class ItemKind : uint32_t {
    CHANGE_REQUEST = 1,
    REQUIREMENT,
    RULE,
    EXTERNAL_INTERFACE,
    DESIGN,
    UNIT_TEST_PLAN,
    MILESTONE,
    UNIT_TEST_RESULT,
    SYSTEM_TEST_PLAN,
    SYSTEM_TEST_RESULT,
    CODE_INSPECTION_RESULT,
    END_TO_END_TEST_PLAN,
    END_TO_END_TEST_RESULT
};

constexpr size_t ITEM_KIND_COUNT = 13;

enum class Step : uint32_t {
    CHANGE_REQUEST_REPORTED = 1,
    REQUIREMENT_DISCUSSING,
    EXTERNAL_INTERFACE_DISCUSSING,
    DESIGN_DISCUSSING,
    UNIT_TEST_PLAN_DISCUSSING,
    DEVELOPMENT_PLAN_DISCUSSING,
    IMPLEMENTING,
    SYSTEM_TEST_PLAN_DISCUSSING,
    SYSTEM_TEST_RUN,
    CODE_INSPECTING,
    END_TO_END_TEST_DISCUSSING,
    END_TO_END_TEST_RUN
};

constexpr size_t STEP_COUNT = 12;

enum class CheckpointState : uint32_t {
    STARTED = 1,
    RUNNING,
    DONE
};

inline bool isValidKind(uint32_t value) { return value >= 1 && value <= ITEM_KIND_COUNT; }
inline bool isValidStep(uint32_t value) { return value >= 1 && value <= STEP_COUNT; }
inline size_t kindIndex(ItemKind kind) { return static_cast<size_t>(kind) - 1; }

std::string_view kindName(ItemKind kind);
uint32_t parseKind(std::string_view name);
std::string_view stateName(CheckpointState state);
uint32_t parseState(std::string_view name);

bool hasDecision(ItemKind kind);

}
