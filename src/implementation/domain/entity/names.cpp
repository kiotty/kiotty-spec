#include "implementation/domain/entity/names.h"


#include <array>
#include <charconv>
#include <cstdio>
#include <utility>

namespace kiotty::spec::implementation {

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
    {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "requirement"},
    {KIOTTY_SPEC_ITEM_KIND_RULE, "rule"},
    {KIOTTY_SPEC_ITEM_KIND_INTERFACE, "interface"},
    {KIOTTY_SPEC_ITEM_KIND_DESIGN, "design"},
    {KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, "unit_test"},
    {KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, "system_test"},
    {KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, "e2e_test"},
    {KIOTTY_SPEC_ITEM_KIND_MILESTONE, "milestone"},
}};

constexpr Table<KiottySpecItemCategory, 15> kCategories{{
    {KIOTTY_SPEC_ITEM_CATEGORY_NONE, "none"},
    {KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL, "functional"},
    {KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY, "quality"},
    {KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT, "environment"},
    {KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE, "architecture"},
    {KIOTTY_SPEC_ITEM_CATEGORY_RULE_CONVENTION, "convention"},
    {KIOTTY_SPEC_ITEM_CATEGORY_RULE_CODING, "coding"},
    {KIOTTY_SPEC_ITEM_CATEGORY_RULE_DEPENDENCY, "dependency"},
    {KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST, "test"},
    {KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW, "view"},
    {KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API, "c_api"},
    {KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE, "module"},
    {KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW, "flow"},
    {KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_DIRECTORY, "directory"},
    {KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MEMORY, "memory"},
}};

constexpr Table<KiottySpecFileRole, 3> kFileRoles{{
    {KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT, "gui_draft"},
    {KIOTTY_SPEC_FILE_ROLE_C_HEADER, "c_header"},
    {KIOTTY_SPEC_FILE_ROLE_SOURCE, "source"},
}};

constexpr Table<KiottySpecLinkType, 5> kLinkTypes{{
    {KIOTTY_SPEC_LINK_TYPE_SATISFIES, "satisfies"},
    {KIOTTY_SPEC_LINK_TYPE_VERIFIES, "verifies"},
    {KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS, "implements"},
    {KIOTTY_SPEC_LINK_TYPE_COVERS, "covers"},
    {KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON, "depends_on"},
}};

constexpr Table<KiottySpecChangeSource, 2> kChangeSources{{
    {KIOTTY_SPEC_CHANGE_SOURCE_FEEDBACK, "feedback"},
    {KIOTTY_SPEC_CHANGE_SOURCE_ISSUE, "issue"},
}};

constexpr Table<KiottySpecChangeState, 4> kChangeStates{{
    {KIOTTY_SPEC_CHANGE_STATE_OPEN, "open"},
    {KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS, "in_progress"},
    {KIOTTY_SPEC_CHANGE_STATE_DONE, "done"},
    {KIOTTY_SPEC_CHANGE_STATE_REJECTED, "rejected"},
}};

constexpr Table<KiottySpecImplementationState, 3> kImplStates{{
    {KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED, "not_implemented"},
    {KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE, "needs_change"},
    {KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED, "implemented"},
}};

constexpr Table<KiottySpecTestResult, 2> kTestResults{{
    {KIOTTY_SPEC_TEST_RESULT_PASS, "pass"},
    {KIOTTY_SPEC_TEST_RESULT_FAIL, "fail"},
}};

constexpr Table<KiottySpecScope, 2> kScopes{{
    {KIOTTY_SPEC_SCOPE_INTRA_MODULE, "intra_module"},
    {KIOTTY_SPEC_SCOPE_INTER_MODULE, "inter_module"},
}};

constexpr Table<KiottySpecDecisionKind, 6> kDecisionKinds{{
    {KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, "requirement"},
    {KIOTTY_SPEC_DECISION_KIND_RULE, "rule"},
    {KIOTTY_SPEC_DECISION_KIND_INTERFACE, "interface"},
    {KIOTTY_SPEC_DECISION_KIND_DESIGN, "design"},
    {KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, "unit_test"},
    {KIOTTY_SPEC_DECISION_KIND_PLAN, "plan"},
}};

constexpr Table<KiottySpecCheckpointPurpose, 2> kPurposes{{
    {KIOTTY_SPEC_CHECKPOINT_PURPOSE_NONE, "none"},
    {KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE, "release"},
}};

constexpr Table<KiottySpecStatus, 10> kStatuses{{
    {KIOTTY_SPEC_STATUS_SUCCESS, "Ok"},
    {KIOTTY_SPEC_STATUS_NOT_FOUND, "NotFound"},
    {KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "InvalidArg"},
    {KIOTTY_SPEC_STATUS_CONFIRMED, "Confirmed"},
    {KIOTTY_SPEC_STATUS_PERMISSION, "Permission"},
    {KIOTTY_SPEC_STATUS_PRECONDITION, "Precondition"},
    {KIOTTY_SPEC_STATUS_CONFLICT, "Conflict"},
    {KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "WrongCursorType"},
    {KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "Io"},
    {KIOTTY_SPEC_STATUS_DATABASE, "Db"},
}};

}

std::string_view name(KiottySpecItemKind value) { return lookupName(kKinds, value); }
std::string_view name(KiottySpecItemCategory value) { return lookupName(kCategories, value); }
std::string_view name(KiottySpecFileRole value) { return lookupName(kFileRoles, value); }
std::string_view name(KiottySpecLinkType value) { return lookupName(kLinkTypes, value); }
std::string_view name(KiottySpecChangeSource value) { return lookupName(kChangeSources, value); }
std::string_view name(KiottySpecChangeState value) { return lookupName(kChangeStates, value); }
std::string_view name(KiottySpecImplementationState value) { return lookupName(kImplStates, value); }
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
bool parse(std::string_view text, KiottySpecImplementationState &out) { return lookupValue(kImplStates, text, out); }
bool parse(std::string_view text, KiottySpecTestResult &out) { return lookupValue(kTestResults, text, out); }
bool parse(std::string_view text, KiottySpecDecisionKind &out) { return lookupValue(kDecisionKinds, text, out); }
bool parse(std::string_view text, KiottySpecCheckpointPurpose &out) { return lookupValue(kPurposes, text, out); }


std::string_view idPrefix(KiottySpecItemKind kind) {
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT: return "REQ";
    case KIOTTY_SPEC_ITEM_KIND_RULE: return "RULE";
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE: return "UI";
    case KIOTTY_SPEC_ITEM_KIND_DESIGN: return "DSN";
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST: return "UT";
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST: return "ST";
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST: return "E2E";
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE: return "MS";
    }
    return "ITEM";
}

std::string_view label(KiottySpecItemKind kind) {
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT: return "요구사항";
    case KIOTTY_SPEC_ITEM_KIND_RULE: return "개발 규칙";
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE: return "외부 인터페이스 항목";
    case KIOTTY_SPEC_ITEM_KIND_DESIGN: return "설계 항목";
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST: return "유닛 테스트";
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST: return "시스템 테스트";
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST: return "E2E 테스트";
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE: return "마일스톤";
    }
    return "항목";
}

std::string_view label(KiottySpecItemCategory category) {
    switch (category) {
    case KIOTTY_SPEC_ITEM_CATEGORY_NONE: return "";
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL: return "기능";
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY: return "품질";
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT: return "환경";
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE: return "아키텍처";
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_CONVENTION: return "컨벤션";
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_CODING: return "코딩 제약";
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_DEPENDENCY: return "의존성";
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST: return "테스트";
    case KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW: return "GUI 뷰";
    case KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API: return "C 함수·타입";
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE: return "모듈";
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW: return "동작 흐름";
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_DIRECTORY: return "디렉토리";
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MEMORY: return "메모리 정책";
    }
    return "";
}

std::string_view label(KiottySpecLinkType type) {
    switch (type) {
    case KIOTTY_SPEC_LINK_TYPE_SATISFIES: return "만족시킨다";
    case KIOTTY_SPEC_LINK_TYPE_VERIFIES: return "검증한다";
    case KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS: return "구현한다";
    case KIOTTY_SPEC_LINK_TYPE_COVERS: return "포함한다";
    case KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON: return "의존한다";
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
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL:
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY:
    case KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT:
        return KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE:
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_CONVENTION:
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_CODING:
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_DEPENDENCY:
    case KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST:
        return KIOTTY_SPEC_ITEM_KIND_RULE;
    case KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW:
    case KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API:
        return KIOTTY_SPEC_ITEM_KIND_INTERFACE;
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE:
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW:
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_DIRECTORY:
    case KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MEMORY:
        return KIOTTY_SPEC_ITEM_KIND_DESIGN;
    case KIOTTY_SPEC_ITEM_CATEGORY_NONE:
        break;
    }
    return static_cast<KiottySpecItemKind>(0);
}

bool isTestKind(KiottySpecItemKind kind) {
    return kind == KIOTTY_SPEC_ITEM_KIND_UNIT_TEST || kind == KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST ||
           kind == KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST;
}

bool isValidActor(KiottySpecActor actor) {
    return actor == KIOTTY_SPEC_ACTOR_USER || actor == KIOTTY_SPEC_ACTOR_AI;
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

}
