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

// ---------------------------------------------------------------------------
// R1 · R2: 원격 상태, 수칙 비교, 내보내기, 편입
// ---------------------------------------------------------------------------

constexpr uint32_t SOURCE_TAG = 0;
constexpr uint64_t COMMON_RULE_SOURCE = uint64_t{1} << 3;
constexpr uint64_t ENTRY_STEP_10 = uint64_t{1} << 9;
constexpr uint32_t AFFECTED_RULE = 1;
constexpr uint32_t AFFECTED_PRIVATE_RULE = 5;
constexpr uint32_t RULE_KIND_TAG = 0;

struct Rule {
    const char* id;
    const char* title;
    const char* body;
    uint64_t tags;
    const char* parent;
};

void writeRule(KiottySpecContext* context, const Rule& rule)
{
    addItem(context, KIOTTY_SPEC_ITEM_KIND_RULE, rule.id, rule.title);
    KiottySpec_writeItemBody(context, textOf(rule.id), textOf(rule.body));
    if (rule.tags != 0) { setTag(context, rule.id, RULE_KIND_TAG, rule.tags); }
    if (rule.parent != nullptr) { KiottySpec_setItemParent(context, textOf(rule.id), textOf(rule.parent)); }
}

KiottySpecContext* openRules(const char* name, const Rule* rules, size_t count)
{
    const KiottySpecContextResult created = openFresh(rootOf(name));
    KiottySpec_startCheckpoint(created.value, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("수칙"));
    for (size_t index = 0; index < count; ++index) { writeRule(created.value, rules[index]); }
    complete(created.value);
    return created.value;
}

void closeProject(KiottySpecContext* context, const char* name)
{
    KiottySpec_releaseContext(context);
    std::error_code error;
    std::filesystem::remove_all(rootOf(name), error);
}

KiottySpecRemote remoteOf(const char* url, const char* base)
{
    KiottySpecRemote remote {};
    std::snprintf(remote.url, sizeof(remote.url), "%s", url);
    std::snprintf(remote.rule_base, sizeof(remote.rule_base), "%s", base);
    return remote;
}

KiottySpecAcceptInput acceptInput(const char* base)
{
    KiottySpecAcceptInput input {};
    input.description = textOf("공통 개발 수칙 abc1234");
    input.title = textOf("공통 개발 수칙 받기");
    input.summary = textOf("원격 수칙 편입");
    input.body = textOf("## 전용 개발 수칙 처리 표\n");
    input.rule_base = textOf(base);
    return input;
}

bool hasRelation(KiottySpecContext* context, const char* from, uint32_t relation, const char* to)
{
    const KiottySpecItemHandleResult from_handle = KiottySpec_findItem(context, textOf(from));
    const KiottySpecItemHandleResult to_handle = KiottySpec_findItem(context, textOf(to));
    if (!ok(from_handle.code) || !ok(to_handle.code)) { return false; }
    const KiottySpecCursorResult cursor = KiottySpec_openRelationCursor(context, textOf(from), KIOTTY_SPEC_DIRECTION_BOTH);
    KiottySpecRelationPage page {};
    KiottySpec_fetchRelationPage(context, cursor.value, &page);
    KiottySpec_closeCursor(context, cursor.value);
    for (size_t index = 0; index < page.length; ++index) {
        const KiottySpecRelation& row = page.elements[index];
        if (row.from == from_handle.value && row.to == to_handle.value && row.relation == relation) { return true; }
    }
    return false;
}

std::string titleOf(KiottySpecContext* context, const char* id)
{
    const KiottySpecItemResult item = KiottySpec_readItem(context, textOf(id));
    return ok(item.code) ? std::string(item.value.title) : std::string();
}

std::string bodyOf(KiottySpecContext* context, const char* id)
{
    static char buffer[KIOTTY_SPEC_MAX_BODY_LENGTH + 1];
    const KiottySpecSizeResult read = KiottySpec_readItemBody(context, textOf(id), KiottySpecBuffer{buffer, sizeof(buffer)});
    return ok(read.code) ? std::string(buffer, read.value) : std::string();
}

bool hasParent(KiottySpecContext* context, const char* id, const char* parent)
{
    const KiottySpecItemResult item = KiottySpec_readItem(context, textOf(id));
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf(parent));
    return ok(item.code) && ok(handle.code) && item.value.parent == handle.value;
}

uint64_t tagOf(KiottySpecContext* context, const char* id, uint32_t tag)
{
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf(id));
    return KiottySpec_readItemTag(context, handle.value, tag).value.value;
}

bool exists(KiottySpecContext* context, const char* id) { return ok(KiottySpec_findItem(context, textOf(id)).code); }

// UT-083: 원격 상태는 체크포인트와 무관하게 쓰이고 다시 열어도 남는다.
bool testRemoteState()
{
    const Rule rules[] = {{"RULE-001", "R1", "본문\n", 0, nullptr}};
    KiottySpecContext* context = openRules("kiotty_rule_remote", rules, 1);
    bool passed = expectTrue(ok(KiottySpec_readRemote(context).code) && KiottySpec_readRemote(context).value.url[0] == '\0', "no remote at first");
    const KiottySpecRemote remote = remoteOf("https://example.com/rules.git", "abc123");
    passed = expectTrue(ok(KiottySpec_writeRemote(context, &remote).code), "write while done") && passed;
    const KiottySpecRemoteResult read = KiottySpec_readRemote(context);
    passed = expectTrue(std::strcmp(read.value.url, remote.url) == 0 && std::strcmp(read.value.rule_base, "abc123") == 0, "read remote") && passed;
    passed = expectTrue(KiottySpec_readPosition(context).value.current == 1, "checkpoint count stays") && passed;
    KiottySpec_releaseContext(context);
    context = openAt(rootOf("kiotty_rule_remote")).value;
    passed = expectTrue(std::strcmp(KiottySpec_readRemote(context).value.url, remote.url) == 0, "remote after reopen") && passed;
    const KiottySpecRemote upper = remoteOf("u", "ABC");
    passed = expectTrue(KiottySpec_writeRemote(context, &upper).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "upper case base") && passed;
    const KiottySpecRemote orphan = remoteOf("", "abc");
    passed = expectTrue(KiottySpec_writeRemote(context, &orphan).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "base without url") && passed;
    KiottySpecRemote unended = remoteOf("", "");
    std::memset(unended.url, 'a', sizeof(unended.url));
    passed = expectTrue(KiottySpec_writeRemote(context, &unended).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "url without nul") && passed;
    const KiottySpecRemote broken = remoteOf("\xff", "");
    passed = expectTrue(KiottySpec_writeRemote(context, &broken).code == KIOTTY_SPEC_STATUS_INVALID_UTF8, "url not utf-8") && passed;
    passed = expectTrue(std::strcmp(KiottySpec_readRemote(context).value.rule_base, "abc123") == 0, "failed writes keep value") && passed;
    const KiottySpecRemote cleared = remoteOf("", "");
    passed = expectTrue(ok(KiottySpec_writeRemote(context, &cleared).code), "clear remote") && passed;
    passed = expectTrue(readAll(databaseOf(rootOf("kiotty_rule_remote"))).find("remote.") == std::string::npos, "no remote lines") && passed;
    closeProject(context, "kiotty_rule_remote");
    return passed;
}

const Rule LOCAL_RULES[] = {
    {"RULE-001", "같음", "같은 본문\n", 1, nullptr},
    {"RULE-002", "로컬에만", "로컬\n", 0, nullptr},
    {"RULE-003", "옛 제목", "본문\n", 0, nullptr},
    {"RULE-004", "본문 다름", "a\nb\nc\n", 0, nullptr},
    {"RULE-005", "태그 다름", "본문\n", 1, nullptr},
    {"RULE-006", "상위 다름", "본문\n", 0, nullptr},
};

const Rule REMOTE_RULES[] = {
    {"RULE-001", "같음", "같은 본문\n", 1, nullptr},
    {"RULE-003", "새 제목", "본문\n", 0, nullptr},
    {"RULE-004", "본문 다름", "a\nB\nc\n", 0, nullptr},
    {"RULE-005", "태그 다름", "본문\n", 2, nullptr},
    {"RULE-006", "상위 다름", "본문\n", 0, "RULE-001"},
    {"RULE-007", "원격에만", "새 수칙\n둘째 줄\n", 0, nullptr},
};

// UT-084: 수칙 차이 커서
bool testRuleDiff()
{
    KiottySpecContext* local = openRules("kiotty_rule_diff_local", LOCAL_RULES, 6);
    KiottySpec_startCheckpoint(local, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("전용"));
    addItem(local, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-001", "전용");
    complete(local);
    KiottySpecContext* remote = openRules("kiotty_rule_diff_remote", REMOTE_RULES, 6);
    const KiottySpecCursorResult cursor = KiottySpec_openRuleDiffCursor(local, remote);
    bool passed = expectTrue(ok(cursor.code) && cursor.total == 6, "six rule rows");
    passed = expectTrue(KiottySpec_openRuleDiffCursor(local, remote).code == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "second rule cursor") && passed;
    KiottySpec_releaseContext(remote);
    KiottySpecRuleDiffPage page {};
    passed = expectTrue(ok(KiottySpec_fetchRuleDiffPage(local, cursor.value, &page).code) && page.length == 6, "page after remote release") && passed;
    const KiottySpecRuleDiff* rows = page.elements;
    passed = expectTrue(std::strcmp(rows[0].id, "RULE-002") == 0 && rows[0].change == KIOTTY_SPEC_CHANGE_REMOVED && std::strcmp(rows[0].title, "로컬에만") == 0, "removed row") && passed;
    passed = expectTrue(std::strcmp(rows[1].id, "RULE-003") == 0 && rows[1].changed_fields == KIOTTY_SPEC_CHANGED_FIELD_TITLE && std::strcmp(rows[1].title, "새 제목") == 0, "title row") && passed;
    passed = expectTrue(rows[2].changed_fields == KIOTTY_SPEC_CHANGED_FIELD_BODY, "body row") && passed;
    passed = expectTrue(rows[3].changed_fields == KIOTTY_SPEC_CHANGED_FIELD_TAGS, "tag row") && passed;
    passed = expectTrue(rows[4].changed_fields == KIOTTY_SPEC_CHANGED_FIELD_PARENT, "parent row") && passed;
    passed = expectTrue(std::strcmp(rows[5].id, "RULE-007") == 0 && rows[5].change == KIOTTY_SPEC_CHANGE_ADDED, "added row") && passed;
    KiottySpec_closeCursor(local, cursor.value);
    remote = openAt(rootOf("kiotty_rule_diff_remote")).value;
    const KiottySpecCursorResult again = KiottySpec_openRuleDiffCursor(local, remote);
    passed = expectTrue(ok(again.code), "reopen after close") && passed;
    KiottySpec_closeCursor(local, again.value);
    passed = expectTrue(KiottySpec_openRuleDiffCursor(local, local).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "same context") && passed;
    KiottySpecConfiguration configuration {};
    KiottySpecContext* unopened = KiottySpec_createContext(&configuration).value;
    passed = expectTrue(KiottySpec_openRuleDiffCursor(local, unopened).code == KIOTTY_SPEC_STATUS_PRECONDITION, "unopened remote") && passed;
    KiottySpec_releaseContext(unopened);
    closeProject(remote, "kiotty_rule_diff_remote");
    closeProject(local, "kiotty_rule_diff_local");
    return passed;
}

std::string joinPieces(KiottySpecContext* context, KiottySpecCursor cursor, KiottySpecTextChange change, bool* is_aligned)
{
    std::string joined;
    KiottySpecTextDiffPage page {};
    do {
        KiottySpec_fetchTextDiffPage(context, cursor, &page);
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecTextDiff& piece = page.elements[index];
            const unsigned char first = static_cast<unsigned char>(piece.text[0]);
            if ((first & 0xC0u) == 0x80u) { *is_aligned = false; }
            if (piece.change == change) { joined.append(piece.text, piece.length); }
        }
    } while (page.length > 0);
    return joined;
}

// UT-085: 본문 차이 커서
bool testRuleBodyDiff()
{
    std::string long_line(254, 'x');
    long_line += "\xEA\xB0\x80\xEB\x82\x98\n";
    const Rule local_rules[] = {{"RULE-004", "본문", "a\nb\nc\n", 0, nullptr}, {"RULE-008", "긴 줄", "짧은 줄\n", 0, nullptr}};
    const Rule remote_rules[] = {{"RULE-004", "본문", "a\nB\nc\n", 0, nullptr},
                                 {"RULE-007", "원격에만", "새 수칙\n둘째 줄\n", 0, nullptr},
                                 {"RULE-008", "긴 줄", long_line.c_str(), 0, nullptr}};
    KiottySpecContext* local = openRules("kiotty_rule_body_local", local_rules, 2);
    KiottySpecContext* remote = openRules("kiotty_rule_body_remote", remote_rules, 3);
    const KiottySpecCursorResult rules = KiottySpec_openRuleDiffCursor(local, remote);
    const KiottySpecCursorResult body = KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-004"));
    bool passed = expectTrue(ok(rules.code) && ok(body.code), "body cursor beside rule cursor");
    passed = expectTrue(KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-004")).code == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "second body cursor") && passed;
    bool is_aligned = true;
    passed = expectTrue(joinPieces(local, body.value, KIOTTY_SPEC_TEXT_CHANGE_REMOVED, &is_aligned) == "b\n", "removed line") && passed;
    KiottySpec_closeCursor(local, body.value);
    const KiottySpecCursorResult kept = KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-004"));
    passed = expectTrue(joinPieces(local, kept.value, KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED, &is_aligned) == "a\nc\n", "unchanged lines") && passed;
    KiottySpec_closeCursor(local, kept.value);
    const KiottySpecCursorResult added = KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-007"));
    passed = expectTrue(joinPieces(local, added.value, KIOTTY_SPEC_TEXT_CHANGE_ADDED, &is_aligned) == "새 수칙\n둘째 줄\n", "remote only body") && passed;
    KiottySpec_closeCursor(local, added.value);
    const KiottySpecCursorResult longer = KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-008"));
    passed = expectTrue(joinPieces(local, longer.value, KIOTTY_SPEC_TEXT_CHANGE_ADDED, &is_aligned) == long_line, "long line joins") && passed;
    passed = expectTrue(is_aligned, "pieces start on utf-8 boundaries") && passed;
    KiottySpec_closeCursor(local, longer.value);
    passed = expectTrue(KiottySpec_openRuleBodyDiffCursor(local, remote, textOf("RULE-999")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing rule") && passed;
    KiottySpec_closeCursor(local, rules.value);
    closeProject(remote, "kiotty_rule_body_remote");
    closeProject(local, "kiotty_rule_body_local");
    return passed;
}

// UT-089: 확정된 체크포인트의 개발 수칙만 내보낸다.
bool testExportRules()
{
    const Rule rules[] = {{"RULE-001", "상위", "상위 본문\n", 1, nullptr}, {"RULE-002", "하위", "하위 본문\n", 4, "RULE-001"}};
    KiottySpecContext* local = openRules("kiotty_rule_export", rules, 2);
    KiottySpec_startCheckpoint(local, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("더함"));
    addItem(local, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-001", "전용");
    addItem(local, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "요구");
    KiottySpec_writeDecision(local, KIOTTY_SPEC_ITEM_KIND_RULE, textOf("결정"));
    const std::string target = rootOf("kiotty_rule_export") + "/out/rule.kisp";
    std::error_code error;
    std::filesystem::create_directories(rootOf("kiotty_rule_export") + "/out", error);
    bool passed = expectTrue(KiottySpec_exportRules(local, textOf(target.c_str())).code == KIOTTY_SPEC_STATUS_PRECONDITION, "running checkpoint");
    complete(local);
    const KiottySpecRemote remote = remoteOf("https://example.com/r.git", "abc");
    KiottySpec_writeRemote(local, &remote);
    const std::string before = readAll(databaseOf(rootOf("kiotty_rule_export")));
    passed = expectTrue(KiottySpec_exportRules(local, textOf("")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "empty path") && passed;
    passed = expectTrue(ok(KiottySpec_exportRules(local, textOf(target.c_str())).code), "export") && passed;
    passed = expectTrue(readAll(databaseOf(rootOf("kiotty_rule_export"))) == before, "local file stays") && passed;
    KiottySpecConfiguration configuration {};
    KiottySpecContext* exported = KiottySpec_createContext(&configuration).value;
    passed = expectTrue(ok(KiottySpec_open(exported, textOf(target.c_str())).code), "open exported") && passed;
    const KiottySpecCheckpointResult checkpoint = KiottySpec_readCheckpoint(exported, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(KiottySpec_readPosition(exported).value.current == 1 && checkpoint.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_DONE &&
                            checkpoint.value.step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, "one done checkpoint") && passed;
    passed = expectTrue(titleOf(exported, "RULE-002") == "하위" && bodyOf(exported, "RULE-002") == "하위 본문\n", "rule title and body") && passed;
    passed = expectTrue(hasParent(exported, "RULE-002", "RULE-001") && tagOf(exported, "RULE-002", RULE_KIND_TAG) == 4, "rule parent and tag") && passed;
    passed = expectTrue(!exists(exported, "PRULE-001") && !exists(exported, "REQ-001"), "only rules") && passed;
    passed = expectTrue(KiottySpec_readRemote(exported).value.url[0] == '\0', "no remote state") && passed;
    char decision[64] {};
    passed = expectTrue(KiottySpec_readDecision(exported, KIOTTY_SPEC_ITEM_KIND_RULE, KiottySpecBuffer{decision, sizeof(decision)}).value == 0, "no decision") && passed;
    KiottySpec_releaseContext(exported);
    closeProject(local, "kiotty_rule_export");
    return passed;
}

KiottySpecContext* openInspected(const char* name, const Rule* rules, size_t count)
{
    KiottySpecContext* context = openRules(name, rules, count);
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_CODE_INSPECTING, textOf("검사"));
    for (size_t index = 0; index < count; ++index) {
        const std::string result = std::string("CIR-") + rules[index].id;
        addItem(context, KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT, result.c_str(), "검사");
        relate(context, result.c_str(), INSPECTED_RULE, rules[index].id);
    }
    complete(context);
    return context;
}

const Rule INSPECTED_RULES[] = {
    {"RULE-001", "그대로", "본문\n", 0, nullptr},
    {"RULE-002", "옛 제목", "본문\n", 0, nullptr},
    {"RULE-003", "지울 수칙", "본문\n", 0, nullptr},
};

const Rule ACCEPTED_RULES[] = {
    {"RULE-001", "그대로", "본문\n", 0, nullptr},
    {"RULE-002", "새 제목", "본문\n", 0, nullptr},
    {"RULE-004", "새 상위", "상위\n", 1, nullptr},
    {"RULE-005", "새 하위", "하위\n", 0, "RULE-004"},
};

// UT-086: 받아 둔 수칙을 쓰기 한 번으로 편입한다.
bool testAcceptRules()
{
    KiottySpecContext* local = openInspected("kiotty_rule_accept_local", INSPECTED_RULES, 3);
    KiottySpecContext* remote = openRules("kiotty_rule_accept_remote", ACCEPTED_RULES, 4);
    const KiottySpecAcceptInput input = acceptInput("abc1234");
    bool passed = expectTrue(KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_AI).code == KIOTTY_SPEC_STATUS_PERMISSION, "ai accept");
    KiottySpecContext* same = openInspected("kiotty_rule_accept_same", INSPECTED_RULES, 3);
    const KiottySpecAcceptResult unchanged = KiottySpec_acceptRules(local, same, &input, KIOTTY_SPEC_ACTOR_USER);
    passed = expectTrue(unchanged.code == KIOTTY_SPEC_STATUS_PRECONDITION && KiottySpec_readLastError(local).value.subject == KIOTTY_SPEC_SUBJECT_REMOTE_RULES, "same rules") && passed;
    closeProject(same, "kiotty_rule_accept_same");
    const std::string remote_path = databaseOf(rootOf("kiotty_rule_accept_remote"));
    KiottySpec_releaseContext(remote);
    const std::string remote_text = readAll(remote_path);
    writeAll(remote_path, replaceFirstLine(remote_text, "#kisp 2 model 1"));
    remote = openAt(rootOf("kiotty_rule_accept_remote")).value;
    passed = expectTrue(KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_USER).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "other model version") && passed;
    KiottySpec_releaseContext(remote);
    writeAll(remote_path, remote_text);
    remote = openAt(rootOf("kiotty_rule_accept_remote")).value;
    KiottySpec_startCheckpoint(local, KIOTTY_SPEC_STEP_CODE_INSPECTING, textOf("진행 중"));
    passed = expectTrue(KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_USER).code == KIOTTY_SPEC_STATUS_PRECONDITION, "running checkpoint") && passed;
    complete(local);
    const KiottySpecAcceptResult accepted = KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_USER);
    passed = expectTrue(ok(accepted.code), "accept") && passed;
    const KiottySpecCheckpointResult checkpoint = KiottySpec_readCheckpoint(local, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(accepted.checkpoint == KiottySpec_readPosition(local).value.current && checkpoint.value.step == KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED &&
                            checkpoint.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING, "running step 1") && passed;
    const char* request = accepted.change_request;
    passed = expectTrue(titleOf(local, request) == "공통 개발 수칙 받기" && bodyOf(local, request) == "## 전용 개발 수칙 처리 표\n", "request text") && passed;
    passed = expectTrue(tagOf(local, request, SOURCE_TAG) == COMMON_RULE_SOURCE && tagOf(local, request, STATE_TAG) == OPEN_STATE &&
                            tagOf(local, request, ENTRY_STEP_TAG) == ENTRY_STEP_10, "request tags") && passed;
    passed = expectTrue(titleOf(local, "RULE-002") == "새 제목" && !exists(local, "RULE-003") && hasParent(local, "RULE-005", "RULE-004") &&
                            tagOf(local, "RULE-004", RULE_KIND_TAG) == 1, "rules follow remote") && passed;
    passed = expectTrue(exists(local, "CIR-RULE-001") && !exists(local, "CIR-RULE-002") && !exists(local, "CIR-RULE-003"), "stale results removed") && passed;
    passed = expectTrue(hasRelation(local, request, AFFECTED_RULE, "RULE-002") && hasRelation(local, request, AFFECTED_RULE, "RULE-004") &&
                            hasRelation(local, request, AFFECTED_RULE, "RULE-005") && !hasRelation(local, request, AFFECTED_RULE, "RULE-001"), "affected rules") && passed;
    passed = expectTrue(std::strcmp(KiottySpec_readRemote(local).value.rule_base, "abc1234") == 0, "base follows accept") && passed;
    closeProject(remote, "kiotty_rule_accept_remote");
    closeProject(local, "kiotty_rule_accept_local");
    KiottySpecContext* early = openRules("kiotty_rule_accept_early", INSPECTED_RULES, 3);
    KiottySpecContext* source = openRules("kiotty_rule_accept_source", ACCEPTED_RULES, 4);
    const KiottySpecAcceptResult early_accept = KiottySpec_acceptRules(early, source, &input, KIOTTY_SPEC_ACTOR_USER);
    passed = expectTrue(ok(early_accept.code) && tagOf(early, early_accept.change_request, ENTRY_STEP_TAG) == (uint64_t{1} << 2), "entry step before inspection") && passed;
    closeProject(source, "kiotty_rule_accept_source");
    closeProject(early, "kiotty_rule_accept_early");
    return passed;
}

// UT-087: 전용 개발 수칙은 내용을 바꾸지 않고, 지워진 수칙과의 연결만 끊긴다.
bool testAcceptPrivateRules()
{
    KiottySpecContext* local = openRules("kiotty_rule_private_local", INSPECTED_RULES, 3);
    KiottySpec_startCheckpoint(local, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("전용"));
    addItem(local, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-001", "P1");
    addItem(local, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-002", "P2");
    addItem(local, KIOTTY_SPEC_ITEM_KIND_PRIVATE_RULE, "PRULE-003", "P3");
    KiottySpec_writeItemBody(local, textOf("PRULE-002"), textOf("P2 본문\n"));
    relate(local, "PRULE-001", REPLACED_RULE, "RULE-003");
    relate(local, "PRULE-002", REPLACED_RULE, "RULE-002");
    relate(local, "PRULE-003", REPLACED_RULE, "RULE-001");
    complete(local);
    KiottySpecContext* remote = openRules("kiotty_rule_private_remote", ACCEPTED_RULES, 4);
    const KiottySpecAcceptInput input = acceptInput("def");
    const KiottySpecAcceptResult accepted = KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_USER);
    bool passed = expectTrue(ok(accepted.code), "accept with private rules");
    passed = expectTrue(titleOf(local, "PRULE-002") == "P2" && bodyOf(local, "PRULE-002") == "P2 본문\n", "private rule content stays") && passed;
    passed = expectTrue(exists(local, "PRULE-001") && !exists(local, "RULE-003"), "private rule stays when its rule is removed") && passed;
    passed = expectTrue(hasRelation(local, "PRULE-002", REPLACED_RULE, "RULE-002"), "modified rule link stays") && passed;
    passed = expectTrue(hasRelation(local, accepted.change_request, AFFECTED_PRIVATE_RULE, "PRULE-001") &&
                            hasRelation(local, accepted.change_request, AFFECTED_PRIVATE_RULE, "PRULE-002"), "affected private rules") && passed;
    passed = expectTrue(!hasRelation(local, accepted.change_request, AFFECTED_PRIVATE_RULE, "PRULE-003"), "untouched private rule") && passed;
    closeProject(remote, "kiotty_rule_private_remote");
    closeProject(local, "kiotty_rule_private_local");
    return passed;
}

// UT-088: 편입 도중 실패하면 아무것도 바뀌지 않는다.
bool testAcceptRollback()
{
    std::error_code error;
    const std::string root = rootOf("kiotty_rule_rollback_local");
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    KiottySpecConfiguration small {};
    small.max_items = 16;
    KiottySpecContext* local = KiottySpec_createContext(&small).value;
    KiottySpec_open(local, textOf(databaseOf(root).c_str()));
    KiottySpec_startCheckpoint(local, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("수칙"));
    for (size_t index = 0; index < 3; ++index) { writeRule(local, INSPECTED_RULES[index]); }
    complete(local);
    std::string ids[14];
    Rule many[17] {};
    for (size_t index = 0; index < 3; ++index) { many[index] = INSPECTED_RULES[index]; }
    for (size_t index = 0; index < 14; ++index) {
        ids[index] = "RULE-1" + std::to_string(10 + index);
        many[3 + index] = Rule {ids[index].c_str(), "새", "본문\n", 0, nullptr};
    }
    KiottySpecContext* remote = openRules("kiotty_rule_rollback_remote", many, 17);
    const std::string before = readAll(databaseOf(root));
    const KiottySpecAcceptInput input = acceptInput("abc");
    const KiottySpecStatus rolled = KiottySpec_acceptRules(local, remote, &input, KIOTTY_SPEC_ACTOR_USER).code;
    bool passed = expectTrue(rolled == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "items run out");
    passed = expectTrue(KiottySpec_readPosition(local).value.current == 1 && !exists(local, "RULE-110"), "nothing applied") && passed;
    passed = expectTrue(KiottySpec_readRemote(local).value.rule_base[0] == '\0', "base unchanged") && passed;
    passed = expectTrue(readAll(databaseOf(root)) == before, "file unchanged") && passed;
    closeProject(remote, "kiotty_rule_rollback_remote");
    KiottySpec_releaseContext(local);
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
        {"remote state", testRemoteState},
        {"rule diff", testRuleDiff},
        {"rule body diff", testRuleBodyDiff},
        {"export rules", testExportRules},
        {"accept rules", testAcceptRules},
        {"accept private rules", testAcceptPrivateRules},
        {"accept rollback", testAcceptRollback},
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
