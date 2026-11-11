#include "kiotty_session.h"

#include "kiotty_path_utf8.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace kiotty::cli {

namespace {

constexpr std::string_view FLAG_NAMES[] = {"tags", "relations", "up", "down", "help"};

bool isFlag(std::string_view name)
{
    for (const std::string_view flag : FLAG_NAMES) {
        if (flag == name) { return true; }
    }
    return false;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts {};
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos) { end = text.size(); }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::string textProblem(const std::string& field, KiottySpecStatus status)
{
    if (status == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) { return field + " 이 상한을 넘음"; }
    if (status == KIOTTY_SPEC_STATUS_INVALID_UTF8) { return field + " 이 UTF-8 이 아님"; }
    return field + " 이 비었거나 null 바이트가 있음";
}

std::string withText(const std::string& sentence, const char* text) { return text[0] == '\0' ? sentence : sentence + ": " + text; }

std::string conditionHeading(KiottySpecCondition condition)
{
    switch (condition) {
    case KIOTTY_SPEC_CONDITION_ENTRY_STEP: return "진입 단계가 정해지지 않은 열린 변경 요청";
    case KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA: return "인수 조건이 없는 기능 요구사항 (R-4)";
    case KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE: return "어떤 마일스톤에도 포함되지 않은 설계 사항 · 유닛 테스트 (R-5)";
    case KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR: return "시스템 테스트와 1:1 이 아닌 기능 요구사항 (R-3)";
    case KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR: return "E2E 테스트와 1:1 이 아닌 외부 인터페이스 (R-3)";
    case KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR: return "코드 검사 결과와 1:1 이 아닌 개발 수칙";
    case KIOTTY_SPEC_CONDITION_SINGLE_TARGET: return "대상이 하나가 아닌 테스트 · 검사 항목";
    case KIOTTY_SPEC_CONDITION_PASSING_RESULT: return "PASS 가 아닌 결과 (R-10)";
    case KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST: return "열린 변경 요청 (R-10)";
    }
    return std::to_string(static_cast<int>(condition));
}

// 어긋난 확정 조건을 조건마다 "- 제목: ID ID" 줄로 잇는다.
std::string unmetConditions(Session& session)
{
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(session.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return std::string(); }
    std::string text {};
    bool first = true;
    KiottySpecCondition current = KIOTTY_SPEC_CONDITION_ENTRY_STEP;
    KiottySpecUnmetConditionPage page {};
    while (KiottySpec_fetchUnmetConditionPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecUnmetCondition& unmet = page.elements[index];
            if (first || unmet.condition != current) { text += "\n- " + conditionHeading(unmet.condition) + ":"; }
            first = false;
            current = unmet.condition;
            text += std::string(" ") + unmet.id;
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return text;
}

std::string completionFailure(Session& session)
{
    const KiottySpecCheckpointResult checkpoint = KiottySpec_readCheckpoint(session.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const std::string step = checkpoint.code == KIOTTY_SPEC_STATUS_SUCCESS ? "Step " + std::to_string(static_cast<int>(checkpoint.value.step)) + " " : "";
    return step + "확정 조건 미충족" + unmetConditions(session);
}

// 라이브러리가 돌려준 (상태, 주제, 글, 줄) 을 문장으로 바꾼다 (개발수칙 5.5).
std::string describeFailure(Session& session, const KiottySpecError& error)
{
    const std::string line = error.line > 0 ? "줄 " + std::to_string(error.line) + ": " : "";
    const KiottySpecStatus status = error.status;
    switch (error.subject) {
    case KIOTTY_SPEC_SUBJECT_NONE: return std::string();
    case KIOTTY_SPEC_SUBJECT_FILE: return line + withText(".kisp 파일이나 디렉토리를 다룰 수 없음", error.text);
    case KIOTTY_SPEC_SUBJECT_FILE_LINE: return line + withText("잘못된 줄 (열 수, 값, 이스케이프, 표 이름)", error.text);
    case KIOTTY_SPEC_SUBJECT_MERGE_MARKER: return line + "병합 충돌 표시가 남아 있음";
    case KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE: return line + withText("같은 기록이 두 줄 (병합을 확인할 것)", error.text);
    case KIOTTY_SPEC_SUBJECT_FILE_CHANGED: return withText(".kisp 가 연 뒤에 밖에서 바뀜 (git pull, 다른 ks). 다시 열고 쓸 것", error.text);
    case KIOTTY_SPEC_SUBJECT_FILE_FORMAT: return line + "첫 줄이 이 ks 의 .kisp 머리줄이 아님 (형식이나 모델 버전이 다름)";
    case KIOTTY_SPEC_SUBJECT_CONFIGURATION: return line + withText("용량 상한에 닿음. 정리하거나 설정을 늘릴 것", error.text);
    case KIOTTY_SPEC_SUBJECT_MODEL: return "모델 표를 적재할 수 없음";
    case KIOTTY_SPEC_SUBJECT_KIND: return "알 수 없거나 결정 기록이 없는 Kind";
    case KIOTTY_SPEC_SUBJECT_ITEM: return error.text[0] == '\0' ? "항목 핸들이 무효임" : withText("없는 항목", error.text);
    case KIOTTY_SPEC_SUBJECT_ID:
        if (status == KIOTTY_SPEC_STATUS_CONFLICT) { return withText("이미 있는 ID", error.text); }
        return withText("ID 는 [A-Za-z0-9] 로 시작하고 [A-Za-z0-9_-] 만 쓰며 15자 이하", error.text);
    case KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY: return withText("지운 항목이 이력에 쓰고 있는 ID (정리 전까지 못 씀)", error.text);
    case KIOTTY_SPEC_SUBJECT_TITLE: return textProblem("제목", status);
    case KIOTTY_SPEC_SUBJECT_SUMMARY: return textProblem("요약", status);
    case KIOTTY_SPEC_SUBJECT_BODY: return textProblem("본문", status);
    case KIOTTY_SPEC_SUBJECT_PAST_BODY: return withText("그 시점의 본문은 지금과 달라 git 이력에만 있음", error.text);
    case KIOTTY_SPEC_SUBJECT_PARENT: return withText("상위 항목이 순환함", error.text);
    case KIOTTY_SPEC_SUBJECT_CHILD: return withText("하위 항목이 있어 지울 수 없음", error.text);
    case KIOTTY_SPEC_SUBJECT_TAG: return line + withText("모델에 없는 태그", error.text);
    case KIOTTY_SPEC_SUBJECT_TAG_VALUE: return line + withText("태그 값이 모델의 태그 정의에 맞지 않음", error.text);
    case KIOTTY_SPEC_SUBJECT_RELATION:
        if (status == KIOTTY_SPEC_STATUS_CONFLICT) { return withText("이미 있는 관계", error.text); }
        if (status == KIOTTY_SPEC_STATUS_NOT_FOUND) { return withText("없는 관계", error.text); }
        return line + withText("모델의 관계 정의에 없는 관계", error.text);
    case KIOTTY_SPEC_SUBJECT_DECISION: return textProblem("결정 기록", status);
    case KIOTTY_SPEC_SUBJECT_CHECKPOINT:
        if (status == KIOTTY_SPEC_STATUS_CONFIRMED) { return "가장 최근 체크포인트가 DONE 임. 새 체크포인트를 시작할 것"; }
        if (status == KIOTTY_SPEC_STATUS_PRECONDITION) { return "체크포인트가 없거나 기준 체크포인트가 DONE 이 아님. 먼저 체크포인트를 시작하거나 확정할 것"; }
        return "없는 체크포인트 (정리로 지워졌거나 아직 없음)";
    case KIOTTY_SPEC_SUBJECT_DESCRIPTION: return textProblem("체크포인트 설명", status);
    case KIOTTY_SPEC_SUBJECT_STEP: return "단계는 1 ~ 12";
    case KIOTTY_SPEC_SUBJECT_ACTOR: return "사용자만 할 수 있음 (R-2)";
    case KIOTTY_SPEC_SUBJECT_CURSOR: return "커서 수가 max_cursors 에 닿음";
    case KIOTTY_SPEC_SUBJECT_CURSOR_ROWS: return "결과 행이 max_cursor_rows 를 넘음. 조건을 좁힐 것";
    case KIOTTY_SPEC_SUBJECT_DIFF_RANGE: return "구간이 거꾸로이거나 비어 있음 (from < to, 시작은 체크포인트)";
    case KIOTTY_SPEC_SUBJECT_TEXT_FIELD: return "글자 diff 는 제목이나 요약 하나에만 쓴다";
    case KIOTTY_SPEC_SUBJECT_PROJECT_NAME: return textProblem("프로젝트 이름", status);
    case KIOTTY_SPEC_SUBJECT_COMPLETION: return completionFailure(session);
    }
    return std::string();
}

}

std::optional<std::string> Arguments::option(std::string_view name) const
{
    std::optional<std::string> found {};
    for (const auto& [key, value] : options) {
        if (key == name) { found = value; }
    }
    return found;
}

std::vector<std::string> Arguments::all(std::string_view name) const
{
    std::vector<std::string> values {};
    for (const auto& [key, value] : options) {
        if (key == name) { values.push_back(value); }
    }
    return values;
}

bool Arguments::flag(std::string_view name) const
{
    for (const auto& value : flags) {
        if (value == name) { return true; }
    }
    return false;
}

Arguments parseArguments(const std::vector<std::string>& tokens, size_t first, const std::vector<std::string_view>& extra_flags)
{
    Arguments args {};
    for (size_t index = first; index < tokens.size(); ++index) {
        const std::string& token = tokens[index];
        if (token.size() > 2 && token.compare(0, 2, "--") == 0) {
            const std::string name = token.substr(2);
            bool extra = false;
            for (const std::string_view flag : extra_flags) { extra = extra || flag == name; }
            if (isFlag(name) || extra || index + 1 >= tokens.size()) {
                args.flags.push_back(name);
            } else {
                args.options.emplace_back(name, tokens[++index]);
            }
            continue;
        }
        args.positional.push_back(token);
    }
    return args;
}

int fail(Session& session, KiottySpecStatus status)
{
    std::cerr << "error: " << KiottySpec_formatStatus(status).value.value;
    const KiottySpecErrorResult last = KiottySpec_readLastError(session.context);
    const bool described = last.code == KIOTTY_SPEC_STATUS_SUCCESS && last.value.status == status && last.value.subject != KIOTTY_SPEC_SUBJECT_NONE;
    if (described) { std::cerr << ": " << describeFailure(session, last.value); }
    std::cerr << "\n";
    return 1;
}

int usage(const std::string& message)
{
    std::cerr << "error: usage: " << message << "\n";
    return 2;
}

KiottySpecText textOf(std::string_view text) { return KiottySpecText{text.data(), text.size()}; }

std::string kindName(KiottySpecItemKind kind) { return KiottySpec_formatKind(kind).value.value; }

std::string stateName(KiottySpecCheckpointState state) { return KiottySpec_formatCheckpointState(state).value.value; }

// 설계서 v1 12 #9. 탭은 공백, 줄바꿈은 \n 두 글자
std::string cell(std::string_view value)
{
    std::string text {};
    text.reserve(value.size());
    for (const char letter : value) {
        if (letter == '\t') {
            text += ' ';
        } else if (letter == '\n') {
            text += "\\n";
        } else if (letter != '\r') {
            text += letter;
        }
    }
    return text.empty() ? "-" : text;
}

void printRow(const std::vector<std::string>& cells)
{
    std::string line {};
    for (size_t index = 0; index < cells.size(); ++index) {
        if (index > 0) { line += '\t'; }
        line += cells[index];
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
}

std::string checkpointName(KiottySpecCheckpointHandle handle)
{
    if (handle == 0) { return "-"; }
    char text[32] {};
    std::snprintf(text, sizeof(text), "CP-%04llu", static_cast<unsigned long long>(handle & 0xFFFFFFFFu));
    return text;
}

std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text)
{
    if (text.size() > 3 && (text.substr(0, 3) == "CP-" || text.substr(0, 3) == "cp-")) { text.remove_prefix(3); }
    if (text.empty() || text.size() > 9) { return std::nullopt; }
    uint64_t value = 0;
    for (const char letter : text) {
        if (letter < '0' || letter > '9') { return std::nullopt; }
        value = value * 10 + static_cast<uint64_t>(letter - '0');
    }
    if (value == 0) { return std::nullopt; }
    return value;
}

std::optional<std::string> readFileText(const std::string& path)
{
    std::ifstream stream(pathFromUtf8(path), std::ios::binary);
    if (!stream) { return std::nullopt; }
    std::ostringstream content {};
    content << stream.rdbuf();
    return content.str();
}

std::optional<KiottySpecItemKind> kindArgument(Session& session, std::string_view text)
{
    const KiottySpecItemKind kind = KiottySpec_parseKind(textOf(text));
    if (kind != 0) { return kind; }
    std::string names {};
    for (uint32_t value = 1; !kindName(static_cast<KiottySpecItemKind>(value)).empty(); ++value) {
        const KiottySpecItemKind candidate = static_cast<KiottySpecItemKind>(value);
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, candidate);
        if (model.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (text == model.value.name) { return candidate; }
        names += (names.empty() ? "" : ", ") + kindName(candidate) + "(" + model.value.name + ")";
    }
    std::cerr << "error: 알 수 없는 Kind '" << text << "'. 받는 값: " << names << "\n";
    return std::nullopt;
}

std::string itemId(Session& session, KiottySpecItemHandle handle)
{
    if (handle == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return "-"; }
    const KiottySpecItemResult item = KiottySpec_readItemByHandle(session.context, handle);
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return "-"; }
    return item.value.id;
}

std::optional<TagColumn> findTag(Session& session, KiottySpecItemKind kind, std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names {};
    for (uint32_t index = 0; model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.tag_count; ++index) {
        TagColumn column {index, {}};
        if (KiottySpec_readTagDefinition(session.context, kind, index, &column.definition).code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (column.definition.name == name) { return column; }
        names += (names.empty() ? "" : ", ") + std::string(column.definition.name);
    }
    std::cerr << "error: " << kindName(kind) << " 에 그런 태그가 없다 (" << name << "). 있는 것: " << (names.empty() ? "없음" : names) << "\n";
    return std::nullopt;
}

std::optional<uint32_t> findRelation(Session& session, KiottySpecItemKind kind, std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names {};
    for (uint32_t index = 0; model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.relation_count; ++index) {
        const KiottySpecRelationDefinitionResult definition = KiottySpec_readRelationDefinition(session.context, kind, index);
        if (definition.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (definition.value.name == name) { return index; }
        names += (names.empty() ? "" : ", ") + std::string(definition.value.name);
    }
    std::cerr << "error: " << kindName(kind) << " 에 그런 관계가 없다 (" << name << "). 있는 것: " << (names.empty() ? "없음" : names) << "\n";
    return std::nullopt;
}

std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition, std::string_view names)
{
    if (names == "-") { return uint64_t{0}; }
    uint64_t value = 0;
    for (const std::string& name : split(names, ',')) {
        bool found = false;
        for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
            if (definition.bit_names[bit][0] != '\0' && name == definition.bit_names[bit]) {
                value |= uint64_t{1} << bit;
                found = true;
            }
        }
        if (!found) {
            std::string known {};
            for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
                if (definition.bit_names[bit][0] != '\0') { known += (known.empty() ? "" : ", ") + std::string(definition.bit_names[bit]); }
            }
            std::cerr << "error: 태그 " << definition.name << " 에 그런 값이 없다 (" << name << "). 있는 것: " << known << "\n";
            return std::nullopt;
        }
    }
    return value;
}

std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value)
{
    std::string text {};
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        if (!text.empty()) { text += '|'; }
        text += definition.bit_names[bit][0] != '\0' ? definition.bit_names[bit] : std::to_string(bit);
    }
    return text.empty() ? "-" : text;
}

}
