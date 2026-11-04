#pragma once

#include "kiotty_spec.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::core {

// 아직 체크포인트가 찍히지 않은(현재 라운드의) 기록이 갖는 cp_seq.
inline constexpr int64_t kPending = std::numeric_limits<int64_t>::max();
// "현재" 뷰를 읽을 때 쓰는 기준값. 미확정 기록까지 포함한다.
inline constexpr int64_t kCurrent = kPending;

// 열거형 <-> 문자열. DB 와 jsonl, CLI 에서 모두 이 이름을 쓴다.
std::string_view name(KiottySpecItemKind value);
std::string_view name(KiottySpecItemCategory value);
std::string_view name(KiottySpecFileRole value);
std::string_view name(KiottySpecLinkType value);
std::string_view name(KiottySpecChangeSource value);
std::string_view name(KiottySpecChangeState value);
std::string_view name(KiottySpecImplState value);
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
bool parse(std::string_view text, KiottySpecImplState &out);
bool parse(std::string_view text, KiottySpecTestResult &out);
bool parse(std::string_view text, KiottySpecDecisionKind &out);
bool parse(std::string_view text, KiottySpecCheckpointPurpose &out);

// 문자열을 열거형으로 바꾸고, 실패하면 InvalidArg 예외를 던진다.
template <class Enum>
Enum parseOrThrow(std::string_view text, std::string_view what);

std::string_view idPrefix(KiottySpecItemKind kind);           // "REQ", "DSN" ...
std::string_view label(KiottySpecItemKind kind);              // "요구사항", "설계 항목" ...
std::string_view label(KiottySpecItemCategory category);      // "기능", "모듈" ...
std::string_view label(KiottySpecLinkType type);              // "만족시킨다" ...
std::string_view stepLabel(int32_t step);                    // 1 -> "요구사항 도출" ...
KiottySpecItemKind kindOfCategory(KiottySpecItemCategory category); // 분류가 속한 종류, None 이면 0
bool isTestKind(KiottySpecItemKind kind);
bool isValidActor(KiottySpecActor actor);

std::string checkpointName(int64_t seq);                       // 42 -> "CP-0042", kPending -> "PENDING"
std::optional<int64_t> parseCheckpointName(std::string_view text); // "CP-0042" / "42" / "PENDING"

} // namespace kiotty::core
