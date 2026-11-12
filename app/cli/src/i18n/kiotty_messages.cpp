#include "kiotty_messages.h"

#include <optional>

namespace kiotty::cli {

namespace {

struct Message {
    MessageKey key {};
    std::string_view text {};
};

constexpr size_t PLACEHOLDER_LENGTH = 3;
constexpr size_t PLACEHOLDER_DIGIT_POSITION = 1;
constexpr char PLACEHOLDER_OPEN = '{';
constexpr char PLACEHOLDER_CLOSE = '}';

constexpr Message MESSAGES[] = {
    { MessageKey::HELP_TEXT,
      "사용법: ks <그룹> [동사] [인자...] [--root DIR] [--actor ai|user]\n"
      "출력은 TSV 이며 첫 줄이 머리글이다. 쓰기 명령은 성공하면 아무것도 찍지 않는다 "
      "(만든 ID 는 찍는다).\n"
      "Kind: change_request requirement rule external_interface design unit_test_plan "
      "milestone unit_test_result\n"
      "      system_test_plan system_test_result code_inspection_result e2e_test_plan "
      "e2e_test_result\n"
      "      한글 모델 이름도 받는다 (`ks model` 의 name 열).\n"
      "DB: <루트>/.kiotty/spec.kisp\n\n" },
    { MessageKey::INIT_SYNOPSIS, "[--name N]" },
    { MessageKey::INIT_SUMMARY, "프로젝트 이름과 만든 시각을 쓴다" },
    { MessageKey::ITEM_ADD_SYNOPSIS,
      "KIND --title T [--id ID] [--summary S] [--parent ID] [--body T|--body-file F] [--tag "
      "이름=값]..." },
    { MessageKey::ITEM_ADD_SUMMARY,
      "항목을 만들고 ID 를 찍는다. --id 가 없으면 발급한다. 이미 있는 ID 는 거부한다" },
    { MessageKey::ITEM_RENAME_SYNOPSIS, "ID 새ID" },
    { MessageKey::ITEM_RENAME_SUMMARY, "ID 만 바꾼다 (관계 · 이력은 그대로 따라온다)" },
    { MessageKey::ITEM_SET_SYNOPSIS,
      "ID [--title T] [--summary S] [--parent ID|-] [--body T|--body-file F] [--tag "
      "이름=값]..." },
    { MessageKey::ITEM_SET_SUMMARY, "항목을 고친다" },
    { MessageKey::ITEM_SHOW_SYNOPSIS,
      "ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]" },
    { MessageKey::ITEM_SHOW_SUMMARY, "항목 한 줄" },
    { MessageKey::ITEM_LIST_SYNOPSIS,
      "[--kind K] [--tag 이름[=값,...]]... [--tags] [--relation 이름]... [--relations]" },
    { MessageKey::ITEM_LIST_SUMMARY, "항목 목록 (8.3, 8.4)" },
    { MessageKey::ITEM_CHILDREN_SYNOPSIS, "ID" },
    { MessageKey::ITEM_CHILDREN_SUMMARY, "하위 항목 목록" },
    { MessageKey::ITEM_BODY_SYNOPSIS, "ID" },
    { MessageKey::ITEM_BODY_SUMMARY, "본문 (본문을 찍는 유일한 명령, 3.7)" },
    { MessageKey::ITEM_REMOVE_SYNOPSIS, "ID" },
    { MessageKey::ITEM_REMOVE_SUMMARY, "항목을 지운다" },
    { MessageKey::TAG_SHOW_SYNOPSIS, "ID" },
    { MessageKey::TAG_SHOW_SUMMARY, "항목의 태그 값" },
    { MessageKey::TAG_SET_SYNOPSIS, "ID 태그이름 값[,값]|-" },
    { MessageKey::TAG_SET_SUMMARY, "태그 값을 바꾼다. - 는 지움" },
    { MessageKey::RELATION_ADD_SYNOPSIS, "FROM 관계이름 TO" },
    { MessageKey::RELATION_ADD_SUMMARY, "관계를 더한다" },
    { MessageKey::RELATION_REMOVE_SYNOPSIS, "FROM 관계이름 TO" },
    { MessageKey::RELATION_REMOVE_SUMMARY, "관계를 지운다" },
    { MessageKey::RELATION_LIST_SYNOPSIS, "ID [--up|--down]" },
    { MessageKey::RELATION_LIST_SUMMARY, "항목의 관계 (깊이 1)" },
    { MessageKey::IMPACT_SYNOPSIS, "ID [--depth N]" },
    { MessageKey::IMPACT_SUMMARY, "이 항목을 가리키는 하류 항목 ID" },
    { MessageKey::CHECKPOINT_START_SYNOPSIS, "STEP [--description D]" },
    { MessageKey::CHECKPOINT_START_SUMMARY, "체크포인트를 시작한다 (STARTED)" },
    { MessageKey::CHECKPOINT_RESUME_SYNOPSIS, "" },
    { MessageKey::CHECKPOINT_RESUME_SUMMARY, "가장 최근 체크포인트를 RUNNING 으로" },
    { MessageKey::CHECKPOINT_COMPLETE_SYNOPSIS, "--actor user" },
    { MessageKey::CHECKPOINT_COMPLETE_SUMMARY, "확정 조건을 검사하고 DONE 으로 (사용자만)" },
    { MessageKey::CHECKPOINT_SHOW_SYNOPSIS, "[CP]" },
    { MessageKey::CHECKPOINT_SHOW_SUMMARY, "체크포인트 한 줄 (기본: 가장 최근)" },
    { MessageKey::CHECKPOINT_LIST_SYNOPSIS, "[--step N]" },
    { MessageKey::CHECKPOINT_LIST_SUMMARY, "체크포인트 목록" },
    { MessageKey::CHECKPOINT_PRUNE_SYNOPSIS, "CP --actor user" },
    { MessageKey::CHECKPOINT_PRUNE_SUMMARY, "CP 이전 기록을 지운다 (사용자만)" },
    { MessageKey::WHERE_SYNOPSIS, "" },
    { MessageKey::WHERE_SUMMARY, "현재 단계" },
    { MessageKey::HISTORY_SYNOPSIS, "[--depth N|all]" },
    { MessageKey::HISTORY_SUMMARY, "현재 체크포인트부터 조상 쪽으로 N 개 (기본 3)" },
    { MessageKey::DECISION_SHOW_SYNOPSIS, "KIND" },
    { MessageKey::DECISION_SHOW_SUMMARY, "결정 기록" },
    { MessageKey::DECISION_SET_SYNOPSIS, "KIND (--text T | --file F)" },
    { MessageKey::DECISION_SET_SUMMARY, "결정 기록을 다시 쓴다 (2048 바이트 이하)" },
    { MessageKey::DIFF_SYNOPSIS,
      "FROM TO [--kind K (--added|--removed|--modified)... [--title] [--summary] [--tag "
      "이름]... [--tags] [--relation 이름]... [--relations] [--no-color]]" },
    { MessageKey::DIFF_SUMMARY, "체크포인트 사이 비교. --kind 가 없으면 Kind 별 요약 (5.4)" },
    { MessageKey::MODEL_SYNOPSIS, "[KIND]" },
    { MessageKey::MODEL_SUMMARY, "Kind 목록, 또는 Kind 의 태그 · 관계 정의" },
    { MessageKey::UNKNOWN_COMMAND, "알 수 없는 명령 '{0}'. `ks help` 참고" },
    { MessageKey::ITEM_ADD_USAGE, "ks item add KIND --title T" },
    { MessageKey::ITEM_SET_USAGE,
      "ks item set ID [--title T] [--summary S] [--parent ID|-] [--body "
      "T|--body-file F] [--tag 이름=값]" },
    { MessageKey::ITEM_SHOW_USAGE,
      "ks item show ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]" },
    { MessageKey::ITEM_RENAME_USAGE, "ks item rename ID 새ID" },
    { MessageKey::ITEM_LIST_COLUMNS_NEED_KIND, "--tag · --relation 은 --kind 와 함께 쓴다" },
    { MessageKey::ITEM_CHILDREN_USAGE, "ks item children ID" },
    { MessageKey::ITEM_BODY_USAGE, "ks item body ID" },
    { MessageKey::ITEM_REMOVE_USAGE, "ks item remove ID" },
    { MessageKey::TAG_SHOW_USAGE, "ks tag show ID" },
    { MessageKey::TAG_SET_USAGE, "ks tag set ID 태그이름 값[,값]|-" },
    { MessageKey::TAG_OPTION_USAGE, "--tag 이름=값[,값]" },
    { MessageKey::RELATION_CHANGE_USAGE, "ks relation add|remove FROM 관계이름 TO" },
    { MessageKey::RELATION_LIST_USAGE, "ks relation list ID [--up|--down]" },
    { MessageKey::IMPACT_USAGE, "ks impact ID [--depth N]" },
    { MessageKey::CHECKPOINT_START_USAGE, "ks checkpoint start STEP(1~12) [--description D]" },
    { MessageKey::CHECKPOINT_LIST_USAGE, "ks checkpoint list [--step N]" },
    { MessageKey::CHECKPOINT_NAME_USAGE, "체크포인트는 CP-0003 또는 3" },
    { MessageKey::CHECKPOINT_PRUNE_USAGE, "ks checkpoint prune CP --actor user" },
    { MessageKey::HISTORY_DEPTH_USAGE, "--depth 는 1 이상의 수나 all" },
    { MessageKey::DECISION_SHOW_USAGE, "ks decision show KIND" },
    { MessageKey::DECISION_SET_USAGE, "ks decision set KIND (--text T | --file F)" },
    { MessageKey::DIFF_USAGE,
      "ks diff FROM TO [--kind K (--added|--removed|--modified)...]. 체크포인트는 "
      "CP-0003, 3, current" },
    { MessageKey::DIFF_GROUP_USAGE, "--added · --removed · --modified 중 하나 이상" },
    { MessageKey::FILE_UNREADABLE, "파일을 읽을 수 없음: {0}" },
    { MessageKey::UNKNOWN_KIND, "알 수 없는 Kind '{0}'. 받는 값: {1}" },
    { MessageKey::UNKNOWN_TAG, "{0} 에 그런 태그가 없다 ({1}). 있는 것: {2}" },
    { MessageKey::UNKNOWN_RELATION, "{0} 에 그런 관계가 없다 ({1}). 있는 것: {2}" },
    { MessageKey::UNKNOWN_TAG_VALUE, "태그 {0} 에 그런 값이 없다 ({1}). 있는 것: {2}" },
    { MessageKey::NOTHING_LISTED, "없음" },
    { MessageKey::TAG_QUERY_LIMIT, "태그 조건은 {0} 개까지" },
    { MessageKey::FAILURE_LINE, "줄 {0}: " },
    { MessageKey::FILE_UNUSABLE, ".kisp 파일이나 디렉토리를 다룰 수 없음" },
    { MessageKey::FILE_LINE_INVALID, "잘못된 줄 (열 수, 값, 이스케이프, 표 이름)" },
    { MessageKey::MERGE_MARKER_LEFT, "병합 충돌 표시가 남아 있음" },
    { MessageKey::DUPLICATE_LINE, "같은 기록이 두 줄 (병합을 확인할 것)" },
    { MessageKey::FILE_CHANGED,
      ".kisp 가 연 뒤에 밖에서 바뀜 (git pull, 다른 ks). 다시 열고 쓸 것" },
    { MessageKey::FILE_FORMAT_MISMATCH,
      "첫 줄이 이 ks 의 .kisp 머리줄이 아님 (형식이나 모델 버전이 다름)" },
    { MessageKey::CAPACITY_REACHED, "용량 상한에 닿음. 정리하거나 설정을 늘릴 것" },
    { MessageKey::MODEL_UNLOADABLE, "모델 표를 적재할 수 없음" },
    { MessageKey::KIND_INVALID, "알 수 없거나 결정 기록이 없는 Kind" },
    { MessageKey::ITEM_HANDLE_INVALID, "항목 핸들이 무효임" },
    { MessageKey::ITEM_MISSING, "없는 항목" },
    { MessageKey::ID_TAKEN, "이미 있는 ID" },
    { MessageKey::ID_INVALID, "ID 는 [A-Za-z0-9] 로 시작하고 [A-Za-z0-9_-] 만 쓰며 15자 이하" },
    { MessageKey::ID_IN_HISTORY, "지운 항목이 이력에 쓰고 있는 ID (정리 전까지 못 씀)" },
    { MessageKey::FIELD_TITLE, "제목" },
    { MessageKey::FIELD_SUMMARY, "요약" },
    { MessageKey::FIELD_BODY, "본문" },
    { MessageKey::FIELD_DECISION, "결정 기록" },
    { MessageKey::FIELD_DESCRIPTION, "체크포인트 설명" },
    { MessageKey::FIELD_PROJECT_NAME, "프로젝트 이름" },
    { MessageKey::PAST_BODY_IN_GIT, "그 시점의 본문은 지금과 달라 git 이력에만 있음" },
    { MessageKey::PARENT_CYCLE, "상위 항목이 순환함" },
    { MessageKey::CHILD_EXISTS, "하위 항목이 있어 지울 수 없음" },
    { MessageKey::TAG_NOT_IN_MODEL, "모델에 없는 태그" },
    { MessageKey::TAG_VALUE_INVALID, "태그 값이 모델의 태그 정의에 맞지 않음" },
    { MessageKey::RELATION_TAKEN, "이미 있는 관계" },
    { MessageKey::RELATION_MISSING, "없는 관계" },
    { MessageKey::RELATION_NOT_IN_MODEL, "모델의 관계 정의에 없는 관계" },
    { MessageKey::CHECKPOINT_DONE, "가장 최근 체크포인트가 DONE 임. 새 체크포인트를 시작할 것" },
    { MessageKey::CHECKPOINT_PRECONDITION,
      "체크포인트가 없거나 기준 체크포인트가 DONE 이 아님. 먼저 체크포인트를 "
      "시작하거나 확정할 것" },
    { MessageKey::CHECKPOINT_MISSING, "없는 체크포인트 (정리로 지워졌거나 아직 없음)" },
    { MessageKey::STEP_RANGE, "단계는 1 ~ 12" },
    { MessageKey::USER_ONLY, "사용자만 할 수 있음 (R-2)" },
    { MessageKey::CURSOR_LIMIT, "커서 수가 max_cursors 에 닿음" },
    { MessageKey::CURSOR_ROWS_LIMIT, "결과 행이 max_cursor_rows 를 넘음. 조건을 좁힐 것" },
    { MessageKey::DIFF_RANGE_INVALID,
      "구간이 거꾸로이거나 비어 있음 (from < to, 시작은 체크포인트)" },
    { MessageKey::TEXT_DIFF_FIELD, "글자 diff 는 제목이나 요약 하나에만 쓴다" },
    { MessageKey::TEXT_TOO_LONG, "{0} 이 상한을 넘음" },
    { MessageKey::TEXT_NOT_UTF8, "{0} 이 UTF-8 이 아님" },
    { MessageKey::TEXT_EMPTY_OR_NULL, "{0} 이 비었거나 null 바이트가 있음" },
    { MessageKey::COMPLETION_UNMET_AT_STEP, "Step {0} 확정 조건 미충족" },
    { MessageKey::COMPLETION_UNMET, "확정 조건 미충족" },
    { MessageKey::CONDITION_ENTRY_STEP, "진입 단계가 정해지지 않은 열린 변경 요청" },
    { MessageKey::CONDITION_ACCEPTANCE_CRITERIA, "인수 조건이 없는 기능 요구사항 (R-4)" },
    { MessageKey::CONDITION_MILESTONE_COVERAGE,
      "어떤 마일스톤에도 포함되지 않은 설계 사항 · 유닛 테스트 (R-5)" },
    { MessageKey::CONDITION_SYSTEM_TEST_PAIR, "시스템 테스트와 1:1 이 아닌 기능 요구사항 (R-3)" },
    { MessageKey::CONDITION_END_TO_END_TEST_PAIR,
      "E2E 테스트와 1:1 이 아닌 외부 인터페이스 (R-3)" },
    { MessageKey::CONDITION_CODE_INSPECTION_PAIR, "코드 검사 결과와 1:1 이 아닌 개발 수칙" },
    { MessageKey::CONDITION_SINGLE_TARGET, "대상이 하나가 아닌 테스트 · 검사 항목" },
    { MessageKey::CONDITION_PASSING_RESULT, "PASS 가 아닌 결과 (R-10)" },
    { MessageKey::CONDITION_CLOSED_CHANGE_REQUEST, "열린 변경 요청 (R-10)" },
    { MessageKey::STEP_CHANGE_REQUEST, "변경 요청 접수" },
    { MessageKey::STEP_REQUIREMENT, "요구사항" },
    { MessageKey::STEP_EXTERNAL_INTERFACE, "외부 인터페이스" },
    { MessageKey::STEP_DESIGN, "설계" },
    { MessageKey::STEP_UNIT_TEST_PLAN, "유닛 테스트 계획" },
    { MessageKey::STEP_DEVELOPMENT_PLAN, "개발 계획" },
    { MessageKey::STEP_IMPLEMENTATION, "구현" },
    { MessageKey::STEP_SYSTEM_TEST_PLAN, "시스템 테스트 계획" },
    { MessageKey::STEP_SYSTEM_TEST_RUN, "시스템 테스트 실행" },
    { MessageKey::STEP_CODE_INSPECTION, "코드 검사" },
    { MessageKey::STEP_END_TO_END_TEST_PLAN, "E2E 테스트 계획" },
    { MessageKey::STEP_END_TO_END_TEST_RUN, "E2E 테스트 실행" },
};

std::optional<std::string_view> findPlaceholderValue(
    std::string_view rest, std::initializer_list<std::string_view> values);

}

std::string formatMessage(MessageKey key, std::initializer_list<std::string_view> values)
{
    const std::string_view pattern = findMessage(key);
    std::string text {};
    size_t start = 0;
    const size_t length = pattern.size();
    while (start < length) {
        const std::string_view rest = pattern.substr(start);
        const std::optional<std::string_view> value = findPlaceholderValue(rest, values);
        if (value) {
            text += *value;
            start += PLACEHOLDER_LENGTH;
            continue;
        }
        text += rest.front();
        ++start;
    }
    return text;
}

std::string_view findMessage(MessageKey key)
{
    for (const Message& message : MESSAGES) {
        if (message.key == key) { return message.text; }
    }
    return std::string_view();
}

namespace {

std::optional<std::string_view> findPlaceholderValue(
    std::string_view rest, std::initializer_list<std::string_view> values)
{
    const std::string_view head = rest.substr(0, PLACEHOLDER_LENGTH);
    const bool has_braces = head.size() == PLACEHOLDER_LENGTH && head.front() == PLACEHOLDER_OPEN &&
                            head.back() == PLACEHOLDER_CLOSE;
    if (!has_braces) { return std::nullopt; }
    const char digit = head.substr(PLACEHOLDER_DIGIT_POSITION, 1).front();
    size_t slot = 0;
    for (const std::string_view value : values) {
        const bool is_slot = digit == static_cast<char>('0' + slot);
        if (is_slot) { return value; }
        ++slot;
    }
    return std::nullopt;
}

}

}
