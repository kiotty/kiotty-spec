// CR-015: 모델 2, 전용 개발 수칙, 원격 수칙 공유를 공개 C 인터페이스로 부른다.

#include "kiotty_spec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace {

constexpr uint32_t STATE_TAG = 1;
constexpr uint32_t ENTRY_STEP_TAG = 2;
constexpr uint64_t OPEN_STATE = uint64_t{1} << 0;
constexpr uint64_t BACKLOG_STATE = uint64_t{1} << 4;
constexpr uint32_t INSPECTED_RULE = 0;
constexpr uint32_t INSPECTED_PRIVATE_RULE = 1;
constexpr uint32_t REPLACED_RULE = 0;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

KiottySpecText textOf(const char* text) { return KiottySpecText{text, std::strlen(text)}; }

bool ok(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

std::string rootOf(const char* name)
{
    std::error_code error;
    return (std::filesystem::temp_directory_path(error) / name).generic_string();
}

std::string databaseOf(const std::string& root) { return root + "/.kiotty/spec.kisp"; }

KiottySpecContextResult openAt(const std::string& root)
{
    KiottySpecConfiguration configuration {};
    KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (!ok(created.code)) { return created; }
    created.code = KiottySpec_open(created.value, textOf(databaseOf(root).c_str())).code;
    return created;
}

KiottySpecContextResult openFresh(const std::string& root)
{
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return openAt(root);
}

std::string readAll(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void writeAll(const std::string& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

std::string replaceFirstLine(const std::string& text, const std::string& line)
{
    const size_t end = text.find('\n');
    return line + text.substr(end);
}

KiottySpecIdResult addItem(KiottySpecContext* context, KiottySpecItemKind kind, const char* id, const char* title)
{
    KiottySpecItemDraft draft {};
    draft.id = textOf(id);
    draft.title = textOf(title);
    draft.kind = kind;
    return KiottySpec_createItem(context, &draft);
}

KiottySpecStatus setTag(KiottySpecContext* context, const char* id, uint32_t tag, uint64_t value)
{
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf(id));
    if (!ok(handle.code)) { return handle.code; }
    return KiottySpec_setItemTag(context, handle.value, tag, value).code;
}

KiottySpecStatus relate(KiottySpecContext* context, const char* from, uint32_t relation, const char* to)
{
    return KiottySpec_addRelation(context, textOf(from), relation, textOf(to)).code;
}

KiottySpecStatus complete(KiottySpecContext* context)
{
    return KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code;
}

bool hasUnmet(KiottySpecContext* context, const char* id, KiottySpecCondition condition)
{
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(context);
    KiottySpecUnmetConditionPage page {};
    KiottySpec_fetchUnmetConditionPage(context, cursor.value, &page);
    KiottySpec_closeCursor(context, cursor.value);
    for (size_t index = 0; index < page.length; ++index) {
        const KiottySpecUnmetCondition& row = page.elements[index];
        if (row.condition == condition && std::strcmp(row.id, id) == 0) { return true; }
    }
    return false;
}

// UT-082: 첫 줄이 모델 1 인 파일은 열리고, 다음 쓰기에서 모델 2 머리줄이 된다.
bool testModelOneFile()
{
    const std::string root = rootOf("kiotty_rule_model_one");
    const KiottySpecContextResult created = openFresh(root);
    KiottySpec_initializeProject(created.value, textOf("model-one"));
    KiottySpec_releaseContext(created.value);
    const std::string path = databaseOf(root);
    const std::string written = readAll(path);
    bool passed = expectTrue(written.rfind("#kisp 2 model 2\n", 0) == 0, "new file is model 2");
    const std::string model_one = replaceFirstLine(written, "#kisp 2 model 1");
    writeAll(path, model_one);
    const KiottySpecContextResult reopened = openAt(root);
    passed = expectTrue(ok(reopened.code), "model 1 file opens") && passed;
    passed = expectTrue(ok(KiottySpec_initializeProject(reopened.value, textOf("model-one")).code), "write model 1 file") && passed;
    KiottySpec_releaseContext(reopened.value);
    const std::string rewritten = readAll(path);
    passed = expectTrue(rewritten.rfind("#kisp 2 model 2\n", 0) == 0, "rewritten header is model 2") && passed;
    const size_t body_start = written.find('\n');
    const std::string old_rest = written.substr(body_start, written.find("project.created_at") - body_start);
    passed = expectTrue(rewritten.find(old_rest) != std::string::npos, "other lines stay") && passed;
    writeAll(path, replaceFirstLine(written, "#kisp 2 model 3"));
    const KiottySpecContextResult newer = openAt(root);
    const KiottySpecErrorResult reason = KiottySpec_readLastError(newer.value);
    passed = expectTrue(newer.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && reason.value.subject == KIOTTY_SPEC_SUBJECT_FILE_FORMAT,
                        "model 3 file is rejected") && passed;
    KiottySpec_releaseContext(newer.value);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return passed;
}

// UT-090: Step 10 은 대체되지 않은 개발 수칙과 모든 전용 개발 수칙이 검사 결과와 1:1 이다.
bool testInspectionPairsWithPrivateRules()
{
    const std::string root = rootOf("kiotty_rule_inspection");
    const KiottySpecContextResult created = openFresh(root);
    KiottySpecContext* const context = created.value;
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_CODE_INSPECTING, textOf("검사"));
    addItem(context, KIOTTY_SPEC_ITEM_KIND_RULE, "RULE-001", "R1");
    addItem(context, KIOTTY_SPEC_ITEM_KIND_RULE, "RULE-002", "R2");
    addItem(context, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-001", "P1");
    bool passed = expectTrue(ok(relate(context, "PRULE-001", REPLACED_RULE, "RULE-001")), "private rule replaces rule");
    addItem(context, KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT, "CIR-002", "R2 검사");
    relate(context, "CIR-002", INSPECTED_RULE, "RULE-002");
    passed = expectTrue(complete(context) == KIOTTY_SPEC_STATUS_PRECONDITION, "private rule without result is rejected") && passed;
    passed = expectTrue(hasUnmet(context, "PRULE-001", KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR), "unmet private rule") && passed;
    passed = expectTrue(!hasUnmet(context, "RULE-001", KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR), "replaced rule is not paired") && passed;
    addItem(context, KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT, "CIR-P01", "P1 검사");
    relate(context, "CIR-P01", INSPECTED_PRIVATE_RULE, "PRULE-001");
    relate(context, "CIR-P01", INSPECTED_RULE, "RULE-001");
    passed = expectTrue(complete(context) == KIOTTY_SPEC_STATUS_PRECONDITION, "result with two targets is rejected") && passed;
    passed = expectTrue(hasUnmet(context, "CIR-P01", KIOTTY_SPEC_CONDITION_SINGLE_TARGET), "unmet single target") && passed;
    KiottySpec_removeRelation(context, textOf("CIR-P01"), INSPECTED_RULE, textOf("RULE-001"));
    passed = expectTrue(ok(complete(context)), "replaced rule needs no result") && passed;
    KiottySpec_releaseContext(context);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return passed;
}

// UT-091: 백로그 변경 요청은 열린 것으로 세지 않고 진입 단계 없이 확정된다.
bool testBacklogChangeRequest()
{
    const std::string root = rootOf("kiotty_rule_backlog");
    const KiottySpecContextResult created = openFresh(root);
    KiottySpecContext* const context = created.value;
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, textOf("접수"));
    addItem(context, KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST, "CR-001", "아이디어");
    bool passed = expectTrue(ok(setTag(context, "CR-001", STATE_TAG, BACKLOG_STATE)), "set backlog state");
    passed = expectTrue(ok(complete(context)), "backlog completes without entry step") && passed;
    const KiottySpecPositionResult position = KiottySpec_readPosition(context);
    passed = expectTrue(ok(position.code) && position.value.open_change_requests == 0, "backlog is not open") && passed;
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, textOf("열기"));
    setTag(context, "CR-001", STATE_TAG, OPEN_STATE);
    passed = expectTrue(complete(context) == KIOTTY_SPEC_STATUS_PRECONDITION, "open needs entry step") && passed;
    passed = expectTrue(hasUnmet(context, "CR-001", KIOTTY_SPEC_CONDITION_ENTRY_STEP), "unmet entry step") && passed;
    setTag(context, "CR-001", ENTRY_STEP_TAG, uint64_t{1} << 1);
    passed = expectTrue(ok(complete(context)), "open with entry step completes") && passed;
    KiottySpec_releaseContext(context);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return passed;
}

}

int main()
{
    const std::pair<const char*, bool (*)()> tests[] = {
        {"model one file", testModelOneFile},
        {"inspection pairs with private rules", testInspectionPairsWithPrivateRules},
        {"backlog change request", testBacklogChangeRequest},
    };
    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
