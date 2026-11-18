#include "kiotty_viewer_names.h"

#include <array>

namespace kiotty::viewer {

namespace {

constexpr size_t CHECKPOINT_DIGIT_WIDTH = 4;
constexpr uint64_t CHECKPOINT_NUMBER_MASK = 0xFFFFFFFFu;

constexpr std::array<std::string_view, 35> SUBJECT_NAMES = {
    "NONE", "FILE", "FILE_LINE", "MERGE_MARKER", "DUPLICATE_LINE", "FILE_CHANGED", "FILE_FORMAT",
    "CONFIGURATION", "MODEL", "KIND", "ITEM", "ID", "ID_IN_HISTORY", "TITLE", "SUMMARY", "BODY",
    "PAST_BODY", "PARENT", "CHILD", "TAG", "TAG_VALUE", "RELATION", "DECISION", "CHECKPOINT",
    "DESCRIPTION", "STEP", "ACTOR", "CURSOR", "CURSOR_ROWS", "DIFF_RANGE", "TEXT_FIELD",
    "PROJECT_NAME", "COMPLETION", "REMOTE", "REMOTE_RULES",
};

constexpr std::array<std::string_view, 10> CONDITION_NAMES = {
    "UNKNOWN", "ENTRY_STEP", "ACCEPTANCE_CRITERIA", "MILESTONE_COVERAGE", "SYSTEM_TEST_PAIR",
    "END_TO_END_TEST_PAIR", "CODE_INSPECTION_PAIR", "SINGLE_TARGET", "PASSING_RESULT",
    "CLOSED_CHANGE_REQUEST",
};

}

static_assert(SUBJECT_NAMES.size() == KIOTTY_SPEC_SUBJECT_REMOTE_RULES + 1, "subject names");
static_assert(CONDITION_NAMES.size() == KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST + 1,
              "condition names");

bool isSuccess(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

KiottySpecText makeText(std::string_view text)
{
    return KiottySpecText { text.data(), text.size() };
}

std::string formatSubject(KiottySpecSubject subject)
{
    const size_t index = static_cast<size_t>(subject);
    const bool is_known = index < SUBJECT_NAMES.size();
    return is_known ? std::string(SUBJECT_NAMES.at(index)) : "UNKNOWN";
}

std::string formatCondition(KiottySpecCondition condition)
{
    const size_t index = static_cast<size_t>(condition);
    const bool is_known = index < CONDITION_NAMES.size();
    return is_known ? std::string(CONDITION_NAMES.at(index)) : "UNKNOWN";
}

std::string formatKind(KiottySpecItemKind kind) { return KiottySpec_formatKind(kind).value.value; }

std::string formatState(KiottySpecCheckpointState state)
{
    return KiottySpec_formatCheckpointState(state).value.value;
}

std::string formatCheckpoint(KiottySpecCheckpointHandle handle)
{
    const std::string digits = std::to_string(handle & CHECKPOINT_NUMBER_MASK);
    const bool is_short = digits.size() < CHECKPOINT_DIGIT_WIDTH;
    const size_t padding = is_short ? CHECKPOINT_DIGIT_WIDTH - digits.size() : 0;
    return "CP-" + std::string(padding, '0') + digits;
}

}
