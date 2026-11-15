#pragma once

#include "presentation/commands/kiotty_commands.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::cli {

enum class GuideSubject { OVERVIEW, STEP, KIND, REMOTE };

struct GuideTopic {
    std::string kind {};
    GuideSubject subject {GuideSubject::OVERVIEW};
    uint32_t step {};
};

struct NextAction {
    std::string action {};
    std::string command {};
};

struct ConditionAdvice {
    std::string reason {};
    std::vector<NextAction> actions {};
};

struct NextState {
    std::vector<KiottySpecUnmetCondition> unmet {};
    KiottySpecPosition position {};
    bool has_database {false};
};

std::optional<GuideTopic> parseGuideTopic(const std::vector<std::string>& words);
std::string formatStepProgress(uint32_t step);
std::optional<ConditionAdvice> findConditionAdvice(KiottySpecCondition condition,
                                                   std::string_view id);
std::vector<NextAction> chooseNextActions(const NextState& state);
std::string findIdPrefix(std::string_view id);

int showGuide(const Session& session);
int showNext(const Session& session);

}
