#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation::entity {

// 설계서 v1 3.1. 값은 1부터이며 kind_mask 의 비트 (kind - 1) 과 Kind별 배열의 index 로 쓴다.
// 5c 에서 공개 헤더의 KiottySpecItemKind 가 같은 값으로 바뀐다.
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

// 설계서 v1 5.2. 정의서 Step 1 · 2 는 REQUIREMENT_DISCUSSING 하나로 합쳐진다.
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

// 설계서 v1 5.1
enum class CheckpointState : uint32_t {
    STARTED = 1,
    RUNNING,
    DONE
};

inline bool isValidKind(uint32_t value) { return value >= 1 && value <= ITEM_KIND_COUNT; }
inline bool isValidStep(uint32_t value) { return value >= 1 && value <= STEP_COUNT; }
inline size_t kindIndex(ItemKind kind) { return static_cast<size_t>(kind) - 1; }

// jsonl 과 ks 가 쓰는 영문 이름 ("design"). 모르는 값이면 빈 문자열, 모르는 이름이면 0.
std::string_view kindName(ItemKind kind);
uint32_t parseKind(std::string_view name);
std::string_view stateName(CheckpointState state);
uint32_t parseState(std::string_view name);

// 결정 기록이 있는 Kind (설계서 v1 6)
bool hasDecision(ItemKind kind);

}
