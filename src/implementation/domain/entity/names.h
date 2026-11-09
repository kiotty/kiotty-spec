#pragma once

#include "kiotty_spec.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

inline constexpr int64_t kPending = std::numeric_limits<int64_t>::max();
inline constexpr int64_t kCurrent = kPending;

std::string_view name(KiottySpecItemKind value);
std::string_view name(KiottySpecItemCategory value);
std::string_view name(KiottySpecFileRole value);
std::string_view name(KiottySpecLinkType value);
std::string_view name(KiottySpecChangeSource value);
std::string_view name(KiottySpecChangeState value);
std::string_view name(KiottySpecImplementationState value);
std::string_view name(KiottySpecTestResult value);
std::string_view name(KiottySpecScope value);
std::string_view name(KiottySpecDecisionKind value);
std::string_view name(KiottySpecCheckpointPurpose value);
std::string_view name(KiottySpecStatus value);

bool parse(std::string_view text, KiottySpecItemKind &out);
bool parse(std::string_view text, KiottySpecItemCategory &out);
bool parse(std::string_view text, KiottySpecFileRole &out);
bool parse(std::string_view text, KiottySpecLinkType &out);
bool parse(std::string_view text, KiottySpecChangeSource &out);
bool parse(std::string_view text, KiottySpecChangeState &out);
bool parse(std::string_view text, KiottySpecImplementationState &out);
bool parse(std::string_view text, KiottySpecTestResult &out);
bool parse(std::string_view text, KiottySpecDecisionKind &out);
bool parse(std::string_view text, KiottySpecCheckpointPurpose &out);

std::string_view idPrefix(KiottySpecItemKind kind);
std::string_view label(KiottySpecItemKind kind);
std::string_view label(KiottySpecItemCategory category);
std::string_view label(KiottySpecLinkType type);
std::string_view stepLabel(int32_t step);
KiottySpecItemKind kindOfCategory(KiottySpecItemCategory category);
bool isTestKind(KiottySpecItemKind kind);
bool isValidActor(KiottySpecActor actor);

std::string checkpointName(int64_t seq);
std::optional<int64_t> parseCheckpointName(std::string_view text);

}
