#include "implementation/core/names.h"

#include "implementation/core/error.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <utility>

namespace kiotty::core {

namespace {

template <class Enum, size_t N>
using Table = std::array<std::pair<Enum, std::string_view>, N>;

template <class Enum, size_t N>
std::string_view lookupName(const Table<Enum, N> &table, Enum value) {
    for (const auto &[entry, text] : table) {
        if (entry == value) {
            return text;
        }
    }
    return "unknown";
}

template <class Enum, size_t N>
bool lookupValue(const Table<Enum, N> &table, std::string_view text, Enum &out) {
    for (const auto &[entry, entryText] : table) {
        if (entryText == text) {
            out = entry;
            return true;
        }
    }
    return false;
}

constexpr Table<KiottySpecItemKind, 8> kKinds{{
    {KiottySpecItemKind_Requirement, "requirement"},
    {KiottySpecItemKind_Rule, "rule"},
    {KiottySpecItemKind_Interface, "interface"},
    {KiottySpecItemKind_Design, "design"},
    {KiottySpecItemKind_UnitTest, "unit_test"},
    {KiottySpecItemKind_SystemTest, "system_test"},
    {KiottySpecItemKind_E2eTest, "e2e_test"},
    {KiottySpecItemKind_Milestone, "milestone"},
}};

constexpr Table<KiottySpecItemCategory, 15> kCategories{{
    {KiottySpecItemCategory_None, "none"},
    {KiottySpecItemCategory_ReqFunctional, "functional"},
    {KiottySpecItemCategory_ReqQuality, "quality"},
    {KiottySpecItemCategory_ReqEnvironment, "environment"},
    {KiottySpecItemCategory_RuleArchitecture, "architecture"},
    {KiottySpecItemCategory_RuleConvention, "convention"},
    {KiottySpecItemCategory_RuleCoding, "coding"},
    {KiottySpecItemCategory_RuleDependency, "dependency"},
    {KiottySpecItemCategory_RuleTest, "test"},
    {KiottySpecItemCategory_UiView, "view"},
    {KiottySpecItemCategory_UiCApi, "c_api"},
    {KiottySpecItemCategory_DsnModule, "module"},
    {KiottySpecItemCategory_DsnFlow, "flow"},
    {KiottySpecItemCategory_DsnDirectory, "directory"},
    {KiottySpecItemCategory_DsnMemory, "memory"},
}};

constexpr Table<KiottySpecFileRole, 3> kFileRoles{{
    {KiottySpecFileRole_GuiDraft, "gui_draft"},
    {KiottySpecFileRole_CHeader, "c_header"},
    {KiottySpecFileRole_Source, "source"},
}};

constexpr Table<KiottySpecLinkType, 5> kLinkTypes{{
    {KiottySpecLinkType_Satisfies, "satisfies"},
    {KiottySpecLinkType_Verifies, "verifies"},
    {KiottySpecLinkType_Implements, "implements"},
    {KiottySpecLinkType_Covers, "covers"},
    {KiottySpecLinkType_DependsOn, "depends_on"},
}};

constexpr Table<KiottySpecChangeSource, 2> kChangeSources{{
    {KiottySpecChangeSource_Feedback, "feedback"},
    {KiottySpecChangeSource_Issue, "issue"},
}};

constexpr Table<KiottySpecChangeState, 4> kChangeStates{{
    {KiottySpecChangeState_Open, "open"},
    {KiottySpecChangeState_InProgress, "in_progress"},
    {KiottySpecChangeState_Done, "done"},
    {KiottySpecChangeState_Rejected, "rejected"},
}};

constexpr Table<KiottySpecImplState, 3> kImplStates{{
    {KiottySpecImplState_NotImplemented, "not_implemented"},
    {KiottySpecImplState_NeedsChange, "needs_change"},
    {KiottySpecImplState_Implemented, "implemented"},
}};

constexpr Table<KiottySpecTestResult, 2> kTestResults{{
    {KiottySpecTestResult_Pass, "pass"},
    {KiottySpecTestResult_Fail, "fail"},
}};

constexpr Table<KiottySpecScope, 2> kScopes{{
    {KiottySpecScope_IntraModule, "intra_module"},
    {KiottySpecScope_InterModule, "inter_module"},
}};

constexpr Table<KiottySpecDecisionKind, 6> kDecisionKinds{{
    {KiottySpecDecisionKind_Requirement, "requirement"},
    {KiottySpecDecisionKind_Rule, "rule"},
    {KiottySpecDecisionKind_Interface, "interface"},
    {KiottySpecDecisionKind_Design, "design"},
    {KiottySpecDecisionKind_UnitTest, "unit_test"},
    {KiottySpecDecisionKind_Plan, "plan"},
}};

constexpr Table<KiottySpecCheckpointPurpose, 2> kPurposes{{
    {KiottySpecCheckpointPurpose_None, "none"},
    {KiottySpecCheckpointPurpose_Release, "release"},
}};

constexpr Table<KiottySpecStatus, 10> kStatuses{{
    {KiottySpecStatus_Ok, "Ok"},
    {KiottySpecStatus_NotFound, "NotFound"},
    {KiottySpecStatus_InvalidArg, "InvalidArg"},
    {KiottySpecStatus_Confirmed, "Confirmed"},
    {KiottySpecStatus_Permission, "Permission"},
    {KiottySpecStatus_Precondition, "Precondition"},
    {KiottySpecStatus_Conflict, "Conflict"},
    {KiottySpecStatus_WrongCursorType, "WrongCursorType"},
    {KiottySpecStatus_Io, "Io"},
    {KiottySpecStatus_Db, "Db"},
}};

} // namespace

std::string_view name(KiottySpecItemKind value) { return lookupName(kKinds, value); }
std::string_view name(KiottySpecItemCategory value) { return lookupName(kCategories, value); }
std::string_view name(KiottySpecFileRole value) { return lookupName(kFileRoles, value); }
std::string_view name(KiottySpecLinkType value) { return lookupName(kLinkTypes, value); }
std::string_view name(KiottySpecChangeSource value) { return lookupName(kChangeSources, value); }
std::string_view name(KiottySpecChangeState value) { return lookupName(kChangeStates, value); }
std::string_view name(KiottySpecImplState value) { return lookupName(kImplStates, value); }
std::string_view name(KiottySpecTestResult value) { return lookupName(kTestResults, value); }
std::string_view name(KiottySpecScope value) { return lookupName(kScopes, value); }
std::string_view name(KiottySpecDecisionKind value) { return lookupName(kDecisionKinds, value); }
std::string_view name(KiottySpecCheckpointPurpose value) { return lookupName(kPurposes, value); }
std::string_view name(KiottySpecStatus value) { return lookupName(kStatuses, value); }

bool parse(std::string_view text, KiottySpecItemKind &out) { return lookupValue(kKinds, text, out); }
bool parse(std::string_view text, KiottySpecItemCategory &out) { return lookupValue(kCategories, text, out); }
bool parse(std::string_view text, KiottySpecFileRole &out) { return lookupValue(kFileRoles, text, out); }
bool parse(std::string_view text, KiottySpecLinkType &out) { return lookupValue(kLinkTypes, text, out); }
bool parse(std::string_view text, KiottySpecChangeSource &out) { return lookupValue(kChangeSources, text, out); }
bool parse(std::string_view text, KiottySpecChangeState &out) { return lookupValue(kChangeStates, text, out); }
bool parse(std::string_view text, KiottySpecImplState &out) { return lookupValue(kImplStates, text, out); }
bool parse(std::string_view text, KiottySpecTestResult &out) { return lookupValue(kTestResults, text, out); }
bool parse(std::string_view text, KiottySpecDecisionKind &out) { return lookupValue(kDecisionKinds, text, out); }
bool parse(std::string_view text, KiottySpecCheckpointPurpose &out) { return lookupValue(kPurposes, text, out); }

template <class Enum>
Enum parseOrThrow(std::string_view text, std::string_view what) {
    Enum value{};
    if (!parse(text, value)) {
        fail(KiottySpecStatus_InvalidArg, "알 수 없는 " + std::string(what) + ": '" + std::string(text) + "'");
    }
    return value;
}

template KiottySpecItemKind parseOrThrow<KiottySpecItemKind>(std::string_view, std::string_view);
template KiottySpecItemCategory parseOrThrow<KiottySpecItemCategory>(std::string_view, std::string_view);
template KiottySpecFileRole parseOrThrow<KiottySpecFileRole>(std::string_view, std::string_view);
template KiottySpecLinkType parseOrThrow<KiottySpecLinkType>(std::string_view, std::string_view);
template KiottySpecChangeSource parseOrThrow<KiottySpecChangeSource>(std::string_view, std::string_view);
template KiottySpecChangeState parseOrThrow<KiottySpecChangeState>(std::string_view, std::string_view);
template KiottySpecImplState parseOrThrow<KiottySpecImplState>(std::string_view, std::string_view);
template KiottySpecTestResult parseOrThrow<KiottySpecTestResult>(std::string_view, std::string_view);
template KiottySpecDecisionKind parseOrThrow<KiottySpecDecisionKind>(std::string_view, std::string_view);
template KiottySpecCheckpointPurpose parseOrThrow<KiottySpecCheckpointPurpose>(std::string_view, std::string_view);

std::string_view idPrefix(KiottySpecItemKind kind) {
    switch (kind) {
    case KiottySpecItemKind_Requirement: return "REQ";
    case KiottySpecItemKind_Rule: return "RULE";
    case KiottySpecItemKind_Interface: return "UI";
    case KiottySpecItemKind_Design: return "DSN";
    case KiottySpecItemKind_UnitTest: return "UT";
    case KiottySpecItemKind_SystemTest: return "ST";
    case KiottySpecItemKind_E2eTest: return "E2E";
    case KiottySpecItemKind_Milestone: return "MS";
    }
    return "ITEM";
}

std::string_view label(KiottySpecItemKind kind) {
    switch (kind) {
    case KiottySpecItemKind_Requirement: return "요구사항";
    case KiottySpecItemKind_Rule: return "개발 규칙";
    case KiottySpecItemKind_Interface: return "외부 인터페이스 항목";
    case KiottySpecItemKind_Design: return "설계 항목";
    case KiottySpecItemKind_UnitTest: return "유닛 테스트";
    case KiottySpecItemKind_SystemTest: return "시스템 테스트";
    case KiottySpecItemKind_E2eTest: return "E2E 테스트";
    case KiottySpecItemKind_Milestone: return "마일스톤";
    }
    return "항목";
}

std::string_view label(KiottySpecItemCategory category) {
    switch (category) {
    case KiottySpecItemCategory_None: return "";
    case KiottySpecItemCategory_ReqFunctional: return "기능";
    case KiottySpecItemCategory_ReqQuality: return "품질";
    case KiottySpecItemCategory_ReqEnvironment: return "환경";
    case KiottySpecItemCategory_RuleArchitecture: return "아키텍처";
    case KiottySpecItemCategory_RuleConvention: return "컨벤션";
    case KiottySpecItemCategory_RuleCoding: return "코딩 제약";
    case KiottySpecItemCategory_RuleDependency: return "의존성";
    case KiottySpecItemCategory_RuleTest: return "테스트";
    case KiottySpecItemCategory_UiView: return "GUI 뷰";
    case KiottySpecItemCategory_UiCApi: return "C 함수·타입";
    case KiottySpecItemCategory_DsnModule: return "모듈";
    case KiottySpecItemCategory_DsnFlow: return "동작 흐름";
    case KiottySpecItemCategory_DsnDirectory: return "디렉토리";
    case KiottySpecItemCategory_DsnMemory: return "메모리 정책";
    }
    return "";
}

std::string_view label(KiottySpecLinkType type) {
    switch (type) {
    case KiottySpecLinkType_Satisfies: return "만족시킨다";
    case KiottySpecLinkType_Verifies: return "검증한다";
    case KiottySpecLinkType_Implements: return "구현한다";
    case KiottySpecLinkType_Covers: return "포함한다";
    case KiottySpecLinkType_DependsOn: return "의존한다";
    }
    return "";
}

std::string_view stepLabel(int32_t step) {
    switch (step) {
    case 1: return "요구사항 도출";
    case 2: return "요구사항 변경";
    case 3: return "외부 인터페이스 도출 및 변경";
    case 4: return "설계 도출 및 변경";
    case 5: return "유닛 테스트 계획 도출 및 변경";
    case 6: return "개발 계획 도출 및 변경";
    case 7: return "구현 및 유닛 테스트";
    case 8: return "시스템 테스트";
    case 9: return "코드 검사";
    case 10: return "E2E 테스트";
    default: return "";
    }
}

KiottySpecItemKind kindOfCategory(KiottySpecItemCategory category) {
    switch (category) {
    case KiottySpecItemCategory_ReqFunctional:
    case KiottySpecItemCategory_ReqQuality:
    case KiottySpecItemCategory_ReqEnvironment:
        return KiottySpecItemKind_Requirement;
    case KiottySpecItemCategory_RuleArchitecture:
    case KiottySpecItemCategory_RuleConvention:
    case KiottySpecItemCategory_RuleCoding:
    case KiottySpecItemCategory_RuleDependency:
    case KiottySpecItemCategory_RuleTest:
        return KiottySpecItemKind_Rule;
    case KiottySpecItemCategory_UiView:
    case KiottySpecItemCategory_UiCApi:
        return KiottySpecItemKind_Interface;
    case KiottySpecItemCategory_DsnModule:
    case KiottySpecItemCategory_DsnFlow:
    case KiottySpecItemCategory_DsnDirectory:
    case KiottySpecItemCategory_DsnMemory:
        return KiottySpecItemKind_Design;
    case KiottySpecItemCategory_None:
        break;
    }
    return static_cast<KiottySpecItemKind>(0);
}

bool isTestKind(KiottySpecItemKind kind) {
    return kind == KiottySpecItemKind_UnitTest || kind == KiottySpecItemKind_SystemTest ||
           kind == KiottySpecItemKind_E2eTest;
}

bool isValidActor(KiottySpecActor actor) {
    return actor == KiottySpecActor_User || actor == KiottySpecActor_Ai;
}

std::string checkpointName(int64_t seq) {
    if (seq == kPending) {
        return "PENDING";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "CP-%04lld", static_cast<long long>(seq));
    return buffer;
}

std::optional<int64_t> parseCheckpointName(std::string_view text) {
    if (text == "PENDING" || text == "pending") {
        return kPending;
    }
    if (text.rfind("CP-", 0) == 0 || text.rfind("cp-", 0) == 0) {
        text.remove_prefix(3);
    }
    int64_t value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value <= 0) {
        return std::nullopt;
    }
    return value;
}

} // namespace kiotty::core
