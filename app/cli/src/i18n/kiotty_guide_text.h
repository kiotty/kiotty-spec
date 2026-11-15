#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kiotty::cli {

struct StepGuide {
    std::string_view role {};
    std::string_view reading {};
    std::string_view linking {};
    std::string_view condition {};
};

struct KindGuide {
    std::string_view purpose {};
    std::string_view body {};
};

struct ConditionText {
    std::string_view reason {};
    std::string_view first_action {};
    std::string_view first_command {};
    std::string_view second_action {};
    std::string_view second_command {};
};

std::string_view findGuideOverview();
std::string_view findGuideRemote();
std::string_view findShortStepName(uint32_t step);
std::optional<StepGuide> findStepGuide(uint32_t step);
std::optional<KindGuide> findKindGuide(uint32_t kind);
std::optional<ConditionText> findConditionText(uint32_t condition);

}
