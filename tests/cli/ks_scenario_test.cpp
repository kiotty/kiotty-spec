// 시스템 테스트 ST-032 ~ ST-044: 빌드한 ks 를 실행해 CR-015 (원격 수칙 공유) 와 CR-016 (ks guide · ks next) 를 본다.
// 사용법: kiotty_ks_scenario_test <ks 경로> [ST-xxx]
#include "infra/kiotty_git.h"
#include "infra/kiotty_path_utf8.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace kiotty::cli;

constexpr std::chrono::seconds LIMIT {120};
const std::string HINT = "처음이면 ks guide, 매 작업 시작에 ks next";
const std::string REQUIREMENT_KIND = "요구사항 종류=기능 요구사항";

std::string g_ks;
fs::path g_base;
bool g_passed = true;

struct Run {
    int code {-1};
    std::string out {};

    bool has(const std::string& text) const { return out.find(text) != std::string::npos; }
};

void check(bool condition, const std::string& label, const std::string& detail = "")
{
    if (condition) { return; }
    std::printf("  FAIL: %s\n", label.c_str());
    if (!detail.empty()) { std::printf("%s\n", detail.c_str()); }
    g_passed = false;
}

Run runCommand(const std::string& program, const std::vector<std::string>& arguments)
{
    const GitRun ran = runProgram(program, arguments, LIMIT, ErrorStream::CAPTURED);
    Run run {};
    run.code = ran.outcome == GitOutcome::FINISHED ? ran.exit_code : -1;
    for (const char letter : ran.output) {
        if (letter != '\r') { run.out += letter; }
    }
    return run;
}

Run ks(const fs::path& root, std::vector<std::string> arguments)
{
    arguments.push_back("--root");
    arguments.push_back(pathToUtf8(root));
    return runCommand(g_ks, arguments);
}

Run user(const fs::path& root, std::vector<std::string> arguments)
{
    arguments.push_back("--actor");
    arguments.push_back("user");
    return ks(root, arguments);
}

Run git(const std::vector<std::string>& arguments) { return runCommand("git", arguments); }

Run must(const Run& run, const std::string& label)
{
    check(run.code == 0, label + " 성공", run.out);
    return run;
}

Run refused(const Run& run, const std::string& reason, const std::string& label)
{
    check(run.code != 0 && run.has(reason), label + " 거부 (" + reason + ")", run.out);
    return run;
}

fs::path fresh(const std::string& name)
{
    std::error_code error {};
    const fs::path directory = g_base / name;
    fs::remove_all(directory, error);
    fs::create_directories(directory, error);
    return directory;
}

fs::path makeProject(const fs::path& directory)
{
    std::error_code error {};
    fs::create_directories(directory, error);
    must(ks(directory, {"init", "--name", pathToUtf8(directory.filename())}), "init");
    return directory;
}

std::string readAll(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream content {};
    content << stream.rdbuf();
    return content.str();
}

void writeAll(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

fs::path specOf(const fs::path& root) { return root / ".kiotty" / "spec.kisp"; }

size_t countOf(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) { ++count; }
    return count;
}

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> lines {};
    std::istringstream stream(text);
    std::string line {};
    while (std::getline(stream, line)) { lines.push_back(line); }
    return lines;
}

size_t countFiles(const fs::path& directory)
{
    std::error_code error {};
    size_t count = 0;
    for (fs::recursive_directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) { ++count; }
    return count;
}

size_t countActionRows(const std::string& text)
{
    const size_t header = text.find("action\tcommand\n");
    if (header == std::string::npos) { return 0; }
    return linesOf(text.substr(header)).size() - 1;
}

void start(const fs::path& root, int step) { must(ks(root, {"checkpoint", "start", std::to_string(step), "--description", "t"}), "start " + std::to_string(step)); }
void complete(const fs::path& root) { must(user(root, {"checkpoint", "complete"}), "complete"); }

void addItem(const fs::path& root, const std::string& kind, const std::string& id, const std::string& title, std::vector<std::string> extra = {})
{
    std::vector<std::string> arguments = {"item", "add", kind, "--id", id, "--title", title};
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    must(ks(root, arguments), "add " + id);
}

void relate(const fs::path& root, const std::string& from, const std::string& relation, const std::string& to)
{
    must(ks(root, {"relation", "add", from, relation, to}), from + " " + relation + " " + to);
}

Run nextUnchanged(const fs::path& root, const std::string& label)
{
    const std::string before = readAll(specOf(root));
    const Run run = ks(root, {"next"});
    check(run.code == 0 && readAll(specOf(root)) == before, label + " ks next 는 사양 파일을 바꾸지 않는다", run.out);
    return run;
}

// ST-032: REQ-050 지금 할 일을 짧게 본다.
void st032()
{
    const fs::path empty = fresh("st032_empty");
    const Run none = ks(empty, {"next"});
    check(none.code == 0 && none.out == "action\tcommand\n사양 DB 를 만든다\tks init --name \"...\"\n", "1 사양 파일이 없으면 ks init 하나", none.out);
    check(countFiles(empty) == 0, "1 폴더에 파일이 생기지 않는다");

    const fs::path root = makeProject(fresh("st032_flow"));
    const Run initialized = nextUnchanged(root, "2");
    check(!initialized.has("CP-") && initialized.has("ks checkpoint start 1 --description \"...\"") && initialized.has("ks guide step 1"), "2 체크포인트가 없으면 Step 1 시작", initialized.out);

    start(root, 1);
    addItem(root, "change_request", "CR-001", "바꿀 것", {"--tag", "상태=열림"});
    const Run entry = nextUnchanged(root, "3");
    const std::vector<std::string> entry_lines = linesOf(entry.out);
    check(entry_lines.size() > 1 && entry_lines[0] == "CP-0001  1 변경 요청 접수  진행 중", "3 첫 줄은 체크포인트 · 단계 · 상태", entry.out);
    check(entry_lines.size() > 1 && entry_lines[1].rfind("변경 요청 (V) → 요구사항", 0) == 0 && countOf(entry_lines[1], "(V)") == 1, "3 진행 줄의 (V) 하나", entry.out);
    check(entry.has("CR-001\tchange_request\t진입 단계 없음") && entry.has("ks tag set CR-001 \"진입 단계\" \"Step N\""), "3 진입 단계 없는 변경 요청과 행동", entry.out);

    must(ks(root, {"tag", "set", "CR-001", "진입 단계", "Step 2"}), "진입 단계");
    const Run met = nextUnchanged(root, "4");
    check(!met.has("id\tkind\treason") && countActionRows(met.out) == 1 && met.has("[user] ks checkpoint complete --actor user"), "4 조건을 채우면 확정 요청 하나", met.out);

    complete(root);
    start(root, 2);
    for (const char* id : {"REQ-001", "REQ-002", "REQ-003"}) { addItem(root, "requirement", id, "할 일", {"--tag", REQUIREMENT_KIND}); }
    const Run criteria = nextUnchanged(root, "5");
    check(countOf(criteria.out, "\trequirement\t인수 조건 없음") == 3 && countActionRows(criteria.out) == 1 && criteria.has("ks item set REQ-001 --body-file F"),
          "5 같은 조건의 항목 셋에 행동 한 줄", criteria.out);

    const fs::path inspected = makeProject(fresh("st032_inspection"));
    start(inspected, 2);
    addItem(inspected, "rule", "RULE-001", "수칙");
    addItem(inspected, "private_rule", "PRULE-001", "전용 수칙");
    complete(inspected);
    start(inspected, 10);
    const Run pairs = nextUnchanged(inspected, "6");
    check(pairs.has("RULE-001\trule\t검사 결과 없음") && pairs.has("PRULE-001\tprivate_rule\t검사 결과 없음") && countActionRows(pairs.out) == 3 &&
              pairs.has("\"검사 개발 수칙\" RULE-001") && pairs.has("\"검사 전용 개발 수칙\" PRULE-001"),
          "6 수칙과 전용 수칙의 검사 결과 행동", pairs.out);

    const fs::path planned = makeProject(fresh("st032_planned"));
    start(planned, 6);
    complete(planned);
    const Run done = nextUnchanged(planned, "7");
    const std::vector<std::string> done_lines = linesOf(done.out);
    check(!done_lines.empty() && done_lines[0].size() > 6 && done_lines[0].substr(done_lines[0].size() - 6) == "확정" && done.has("개발 계획 (V) → 구현") &&
              done.has("ks checkpoint start 7 --description \"...\"") && done.has("ks guide step 7"),
          "7 Step 6 확정 뒤 Step 7 시작", done.out);

    const fs::path finished = makeProject(fresh("st032_finished"));
    start(finished, 12);
    complete(finished);
    check(nextUnchanged(finished, "8").has("할 일 없음\t-"), "8 Step 12 확정이면 할 일 없음");

    const fs::path backlog = makeProject(fresh("st032_backlog"));
    start(backlog, 12);
    addItem(backlog, "change_request", "CR-001", "나중에", {"--tag", "상태=백로그"});
    complete(backlog);
    check(nextUnchanged(backlog, "9").has("할 일 없음\t-"), "9 백로그 변경 요청만 있으면 할 일 없음");

    std::error_code error {};
    const fs::path below = finished / "sub" / "deeper";
    fs::create_directories(below, error);
    const fs::path previous = fs::current_path(error);
    fs::current_path(below, error);
    const Run nested = runCommand(g_ks, {"next"});
    fs::current_path(previous, error);
    check(nested.code == 0 && nested.out.rfind("CP-0001  12 ", 0) == 0 && !fs::exists(finished / "sub" / ".kiotty") && !fs::exists(below / ".kiotty"),
          "11 하위 폴더에서 위 폴더의 사양 파일을 쓴다", nested.out);
}

// ST-033: REQ-048 ks 의 목적과 작업 방법을 한 번에 읽는다.
void st033()
{
    const fs::path empty = fresh("st033");
    const Run guide = ks(empty, {"guide"});
    check(guide.code == 0, "1 종료 코드 0");
    for (const char* part : {"[목적]", "\n1. ", "\n2. ", "\n3. ", "[역할]", "[12 단계]", "[작업 루프]", "[규칙]"}) {
        check(guide.has(part), std::string("1 ") + part, guide.out);
    }
    for (const char* step : {" 1 변경 요청 접수", " 2 요구사항", " 3 외부 인터페이스", " 4 설계", " 5 유닛 테스트 계획", " 6 개발 계획", " 7 구현",
                             " 8 시스템 테스트 계획", " 9 시스템 테스트 실행", "10 코드 검사", "11 E2E 테스트 계획", "12 E2E 테스트 실행"}) {
        check(guide.has(std::string("\n") + step), std::string("1 단계 줄") + step);
    }
    bool has_rules = false;
    for (const std::string& line : linesOf(guide.out)) {
        if (line.rfind(" 2 요구사항", 0) == 0) { has_rules = line.find("REQ, RULE, PRULE") != std::string::npos; }
    }
    check(has_rules, "2 Step 2 줄에 RULE, PRULE");
    for (const char* command : {"checkpoint complete", "prune", "remote push", "accept", "discard"}) {
        check(guide.has(command), std::string("2 사용자 몫 ") + command);
    }
    const Run remote = ks(empty, {"guide", "remote"});
    for (const char* verb : {"ks remote accept", "ks remote discard", "ks remote push"}) {
        bool is_user = false;
        for (const std::string& line : linesOf(remote.out)) {
            if (line.find(verb) != std::string::npos) { is_user = line.rfind("[user] ", 0) == 0; }
        }
        check(remote.code == 0 && is_user, std::string("3 [user] ") + verb, remote.out);
    }
    check(countFiles(empty) == 0, "1 · 3 폴더에 파일이 생기지 않는다");
    for (const std::vector<std::string>& topic : {std::vector<std::string>{"guide", "other"}, {"guide", "step"}, {"guide", "remote", "rule"}}) {
        const Run wrong = ks(empty, topic);
        check(wrong.code == 2 && wrong.has("ks guide [step N (1 ~ 12) | kind KIND | remote]"), "4 잘못된 주제", wrong.out);
    }
}

std::vector<std::vector<std::string>> modelRows(const fs::path& root, const std::string& kind)
{
    std::vector<std::vector<std::string>> rows {};
    const std::vector<std::string> arguments = kind.empty() ? std::vector<std::string>{"model"} : std::vector<std::string>{"model", kind};
    const std::vector<std::string> lines = linesOf(ks(root, arguments).out);
    for (size_t index = 1; index < lines.size(); ++index) {
        std::vector<std::string> cells {};
        std::istringstream stream(lines[index]);
        std::string cell {};
        while (std::getline(stream, cell, '\t')) { cells.push_back(cell); }
        rows.push_back(cells);
    }
    return rows;
}

bool hasModel(const Run& guide, const std::vector<std::vector<std::string>>& rows)
{
    for (const std::vector<std::string>& row : rows) {
        const bool is_tag = row.size() > 3 && row[0] == "tag";
        const std::string line = is_tag ? "태그  " + row[2] + " = " + row[3] : "관계  " + row[2] + " → " + row[3];
        if (!guide.has(line)) { return false; }
    }
    return true;
}

// ST-034: REQ-049 단계와 항목 종류마다 무엇을 만들고 쓰는지 읽는다.
void st034()
{
    const fs::path root = makeProject(fresh("st034"));
    const std::vector<std::vector<std::string>> kinds = modelRows(root, "");
    for (int step = 1; step <= 12; ++step) {
        const Run guide = ks(root, {"guide", "step", std::to_string(step)});
        const std::vector<std::string> lines = linesOf(guide.out);
        bool has_kinds = true;
        for (const std::vector<std::string>& kind : kinds) {
            const bool is_in_step = kind.size() > 3 && kind[3].rfind(std::to_string(step) + " ", 0) == 0;
            if (is_in_step) { has_kinds = has_kinds && guide.has(kind[0] + " (" + kind[2] + ")"); }
        }
        check(guide.code == 0 && !lines.empty() && lines[0].rfind("Step " + std::to_string(step) + " ", 0) == 0 && lines[0].find(" — ") != std::string::npos &&
                  guide.has("읽을 것: ") && guide.has("변경 요청 · 항목에 잇기: ") && guide.has("확정 조건: ") && has_kinds,
              "1 step " + std::to_string(step), guide.out);
    }
    const Run requirements = ks(root, {"guide", "step", "2"});
    check(requirements.has("requirement (REQ)") && requirements.has("rule (RULE)") && requirements.has("private_rule (PRULE)") &&
              hasModel(requirements, modelRows(root, "rule")),
          "2 Step 2 의 수칙 블록과 수칙 종류 값", requirements.out);
    for (const std::vector<std::string>& kind : kinds) {
        if (kind.size() < 2) { continue; }
        const Run english = ks(root, {"guide", "kind", kind[0]});
        const Run korean = ks(root, {"guide", "kind", kind[1]});
        check(english.code == 0 && english.out == korean.out && hasModel(english, modelRows(root, kind[0])), "3 kind " + kind[0], english.out);
    }
    const Run zero = ks(root, {"guide", "step", "0"});
    const Run over = ks(root, {"guide", "step", "13"});
    const Run nope = ks(root, {"guide", "kind", "nope"});
    check(zero.code == 2 && over.code == 2 && zero.has("1 ~ 12") && over.has("1 ~ 12"), "4 범위 밖 단계", zero.out + over.out);
    check(nope.code == 2 && nope.has("change_request(변경 요청)"), "4 없는 Kind 는 받는 값을 알린다", nope.out);
}

// ST-035: REQ-051 처음 마주치는 곳에서 안내를 알린다.
void st035()
{
    const Run help = runCommand(g_ks, {"help"});
    check(help.code == 0 && help.out.rfind(HINT + "\n", 0) == 0, "1 ks help 첫 줄", help.out.substr(0, 200));
    const fs::path empty = fresh("st035");
    const Run initialized = ks(empty, {"init", "--name", "x"});
    check(initialized.code == 0 && initialized.out == HINT + "\n", "2 ks init 뒤 한 줄", initialized.out);
}

std::vector<std::string> splitCommand(const std::string& command)
{
    std::vector<std::string> words {};
    std::string word {};
    bool is_quoted = false;
    bool has_word = false;
    for (const char letter : command) {
        if (letter == '"') {
            is_quoted = !is_quoted;
            has_word = true;
            continue;
        }
        if (letter == ' ' && !is_quoted) {
            if (has_word) { words.push_back(word); }
            word.clear();
            has_word = false;
            continue;
        }
        word += letter;
        has_word = true;
    }
    if (has_word) { words.push_back(word); }
    return words;
}

std::string firstCommand(const Run& next)
{
    const std::vector<std::string> lines = linesOf(next.out.substr(next.out.find("action\tcommand\n")));
    return lines.size() > 1 ? lines[1].substr(lines[1].find('\t') + 1) : std::string();
}

Run follow(const fs::path& root, const std::string& command, const fs::path& body_file)
{
    std::vector<std::string> words = splitCommand(command);
    const bool is_user = !words.empty() && words.front() == "[user]";
    if (is_user) { words.erase(words.begin()); }
    words.erase(words.begin());
    for (std::string& word : words) {
        if (word == "...") { word = "t"; }
        if (word == "Step N") { word = "Step 2"; }
        if (word == "F") { word = pathToUtf8(body_file); }
    }
    return is_user ? user(root, words) : ks(root, words);
}

// ST-036: REQ-047 ks next 만 따라 Step 1 에서 Step 2 까지 간다.
void st036()
{
    const fs::path root = fresh("st036");
    const fs::path body_file = g_base / "st036_body.md";
    writeAll(body_file, "## 진술\n할 일\n\n## 인수 조건\n- 전제 조건: a / 행동: b / 기대 결과: c\n");
    std::vector<std::string> followed {};
    size_t user_lines = 0;
    for (int round = 0; round < 8; ++round) {
        const Run next = ks(root, {"next"});
        const std::string command = firstCommand(next);
        const bool is_step_two = next.has("CP-0002  2 요구사항  진행 중");
        if (is_step_two || command.empty()) { break; }
        followed.push_back(command);
        user_lines += command.rfind("[user] ", 0) == 0 ? 1 : 0;
        must(follow(root, command, body_file), "따라 실행: " + command);
        const bool is_first_step = command.rfind("ks checkpoint start 1", 0) == 0;
        if (is_first_step) { addItem(root, "change_request", "CR-001", "요청", {"--tag", "상태=열림"}); }
    }
    const std::vector<std::string> expected = {"ks init --name \"...\"", "ks checkpoint start 1 --description \"...\"", "ks tag set CR-001 \"진입 단계\" \"Step N\"",
                                               "[user] ks checkpoint complete --actor user", "ks checkpoint start 2 --description \"...\""};
    std::string trail {};
    for (const std::string& command : followed) { trail += "    " + command + "\n"; }
    check(followed == expected && user_lines == 1, "1 init → Step 1 → 진입 단계 → 확정 → Step 2", trail);
    addItem(root, "requirement", "REQ-001", "할 일", {"--tag", REQUIREMENT_KIND});
    const Run criteria = ks(root, {"next"});
    check(criteria.has("REQ-001\trequirement\t인수 조건 없음") && firstCommand(criteria) == "ks item set REQ-001 --body-file F", "2 인수 조건 없는 요구사항", criteria.out);
    must(follow(root, firstCommand(criteria), body_file), "2 본문 쓰기");
    check(firstCommand(ks(root, {"next"})) == "[user] ks checkpoint complete --actor user", "2 본문을 쓰면 확정 요청");
}

struct Remote {
    fs::path directory {};
    fs::path origin {};
    std::string url {};
};

Remote makeRemote(const std::string& name)
{
    Remote remote {};
    remote.directory = fresh(name);
    remote.origin = remote.directory / "origin.git";
    must(git({"init", "--quiet", "--bare", pathToUtf8(remote.origin)}), "git init --bare");
    const std::string path = pathToUtf8(remote.origin);
    remote.url = path.front() == '/' ? "file://" + path : "file:///" + path;
    return remote;
}

std::string originHead(const Remote& remote)
{
    const Run head = git({"--git-dir", pathToUtf8(remote.origin), "rev-parse", "--verify", "--quiet", "HEAD"});
    return head.code == 0 ? linesOf(head.out).front() : std::string();
}

std::string originFile(const Remote& remote) { return git({"--git-dir", pathToUtf8(remote.origin), "show", "HEAD:rule.kisp"}).out; }

fs::path makeRulesProject(const Remote& remote, const std::string& name)
{
    const fs::path root = makeProject(remote.directory / name);
    start(root, 2);
    addItem(root, "rule", "RULE-001", "첫 수칙", {"--body", "a\nb\nc", "--tag", "수칙 종류=컨벤션"});
    addItem(root, "rule", "RULE-002", "둘째 수칙", {"--body", "x"});
    addItem(root, "rule", "RULE-003", "셋째 수칙", {"--body", "gone"});
    complete(root);
    must(ks(root, {"remote", "open", remote.url}), name + " remote open");
    return root;
}

fs::path makeEmptyProject(const Remote& remote, const std::string& name)
{
    const fs::path root = makeProject(remote.directory / name);
    start(root, 2);
    complete(root);
    must(ks(root, {"remote", "open", remote.url}), name + " remote open");
    return root;
}

void pullAndAccept(const fs::path& root)
{
    must(ks(root, {"remote", "pull", "rule"}), "pull");
    must(user(root, {"remote", "accept", "rule"}), "accept");
    complete(root);
}

std::string withoutColumn(const std::string& text, size_t column)
{
    std::string kept {};
    for (const std::string& line : linesOf(text)) {
        std::istringstream stream(line);
        std::string cell {};
        size_t index = 0;
        while (std::getline(stream, cell, '\t')) {
            if (index != column) { kept += cell + "\t"; }
            ++index;
        }
        kept += "\n";
    }
    return kept;
}

std::string ruleSnapshot(const fs::path& root)
{
    std::string snapshot = withoutColumn(ks(root, {"item", "list", "--kind", "rule", "--tags"}).out, 4);
    for (const char* id : {"RULE-001", "RULE-002", "RULE-003", "RULE-004", "RULE-076"}) { snapshot += ks(root, {"item", "body", id}).out; }
    return snapshot;
}

void changeRules(const fs::path& root)
{
    start(root, 2);
    must(ks(root, {"item", "set", "RULE-001", "--body", "a\nB\nc"}), "RULE-001 본문");
    must(ks(root, {"item", "set", "RULE-002", "--title", "둘째 수칙 v2"}), "RULE-002 제목");
    must(ks(root, {"item", "remove", "RULE-003"}), "RULE-003 삭제");
    addItem(root, "rule", "RULE-004", "넷째 수칙", {"--body", "new"});
    complete(root);
}

// ST-037: REQ-039 개발 수칙을 다른 프로젝트와 나눈다.
void st037()
{
    const Remote remote = makeRemote("st037");
    const fs::path a = makeRulesProject(remote, "A");
    const fs::path b = makeEmptyProject(remote, "B");
    must(user(a, {"remote", "push", "rule"}), "A push");
    check(ks(b, {"remote", "check", "rule"}).has("changed"), "1 B 가 바뀐 것을 안다");
    must(ks(b, {"remote", "pull", "rule"}), "1 B pull");
    check(countOf(ks(b, {"remote", "diff", "rule"}).out, "\tadded\t") == 3, "1 diff 에 추가 셋");
    must(user(b, {"remote", "accept", "rule"}), "1 B accept");
    check(ruleSnapshot(a) == ruleSnapshot(b), "1 B 의 수칙이 A 와 같다", ruleSnapshot(a) + "----\n" + ruleSnapshot(b));
    complete(b);

    start(a, 2);
    addItem(a, "private_rule", "PRULE-001", "A 전용");
    complete(a);
    must(user(a, {"remote", "push", "rule"}), "2 A push");
    const std::string pushed = originFile(remote);
    check(!pushed.empty() && pushed.find("PRULE") == std::string::npos && pushed.find("private_rule") == std::string::npos, "2 원격에 전용 개발 수칙이 없다");

    const std::string before = ruleSnapshot(b);
    start(a, 2);
    must(ks(a, {"item", "set", "RULE-002", "--title", "둘째 v3"}), "3 A 수정");
    complete(a);
    must(user(a, {"remote", "push", "rule"}), "3 A push");
    check(ruleSnapshot(b) == before, "3 B 는 자동으로 받지 않는다");
    check(ks(b, {"remote", "check", "rule"}).out.rfind("changed", 0) == 0, "3 B 의 check 는 changed");
}

// ST-038: REQ-040 원격 저장소 주소를 기억하고 잊는다.
void st038()
{
    const Remote remote = makeRemote("st038");
    const fs::path a = makeRulesProject(remote, "A");
    must(user(a, {"remote", "push", "rule"}), "A push");
    const fs::path b = makeProject(remote.directory / "B");
    start(b, 2);
    complete(b);
    refused(ks(b, {"remote", "check", "rule"}), "원격이 없음", "4 주소 없이 check");
    refused(ks(b, {"remote", "pull", "rule"}), "원격이 없음", "4 주소 없이 pull");
    const std::string checkpoints = ks(b, {"checkpoint", "list"}).out;
    must(ks(b, {"remote", "open", remote.url}), "1 open");
    check(ks(b, {"checkpoint", "list"}).out == checkpoints, "3 확정된 뒤에 open 해도 체크포인트는 그대로");
    check(fs::is_directory(b / ".kiotty" / "rule" / ".git") && readAll(b / ".kiotty" / ".gitignore").find("rule/") != std::string::npos, "1 복제본과 .gitignore");
    must(ks(b, {"remote", "check", "rule"}), "1 check");
    must(ks(b, {"remote", "pull", "rule"}), "2 pull");
    must(ks(b, {"remote", "list", "rule"}), "2 받아 둔 수칙");
    must(ks(b, {"remote", "close"}), "2 close");
    refused(ks(b, {"remote", "check", "rule"}), "원격이 없음", "2 close 뒤 check");
    check(ks(b, {"remote", "list", "rule"}).code != 0 && !fs::exists(b / ".kiotty" / "rule"), "2 받아 둔 수칙과 복제본이 없다");
}

// ST-039: REQ-041 확정된 체크포인트의 개발 수칙만 올린다.
void st039()
{
    const Remote remote = makeRemote("st039");
    const fs::path a = makeRulesProject(remote, "A");
    start(a, 2);
    refused(user(a, {"remote", "push", "rule"}), "DONE", "1 진행 중이면");
    check(originHead(remote).empty(), "1 원격 commit 없음");
    complete(a);
    refused(ks(a, {"remote", "push", "rule"}), "사용자만", "2 AI 의 push");
    must(user(a, {"remote", "push", "rule"}), "3 push");
    const std::string first = originHead(remote);
    const Run message = git({"--git-dir", pathToUtf8(remote.origin), "log", "-1", "--format=%s"});
    check(!first.empty() && ks(a, {"remote", "check", "rule"}).out.rfind("same", 0) == 0, "3 원격 commit 이 기준 버전");
    check(message.has("A") && message.has("CP-"), "3 commit 메시지에 프로젝트 이름과 체크포인트", message.out);

    const fs::path b = makeEmptyProject(remote, "B");
    pullAndAccept(b);
    start(b, 2);
    addItem(b, "rule", "RULE-009", "B 의 수칙");
    complete(b);
    must(user(b, {"remote", "push", "rule"}), "4 B push");
    const std::string moved = originHead(remote);
    refused(user(a, {"remote", "push", "rule"}), "원격", "4 A 의 기준 버전 뒤에 B 가 올렸다");
    check(originHead(remote) == moved && moved != first, "4 원격 HEAD 가 그대로");
    must(user(b, {"remote", "push", "rule"}), "5 수칙이 같은 채 두 번째 push");
}

// ST-040: REQ-042 원격이 바뀌었는지 확인하고 받아 둔다.
void st040()
{
    const Remote remote = makeRemote("st040");
    const fs::path a = makeRulesProject(remote, "A");
    must(user(a, {"remote", "push", "rule"}), "A push");
    const fs::path b = makeEmptyProject(remote, "B");
    pullAndAccept(b);
    check(ks(b, {"remote", "check", "rule"}).out.rfind("same", 0) == 0, "1 기준 버전 = 원격이면 same");
    refused(ks(b, {"remote", "list", "rule"}), "받아 둔 수칙이 없음", "5 accept 뒤");
    start(a, 2);
    addItem(a, "rule", "RULE-004", "넷째 수칙", {"--body", "new"});
    complete(a);
    must(user(a, {"remote", "push", "rule"}), "A push 2");
    const Run changed = ks(b, {"remote", "check", "rule"});
    check(changed.out == "changed\t" + originHead(remote) + "\n", "2 changed 와 새 commit", changed.out);
    const std::string rules = ruleSnapshot(b);
    const std::string checkpoints = ks(b, {"checkpoint", "list"}).out;
    const Run pulled = must(ks(b, {"remote", "pull", "rule"}), "3 pull");
    check(pulled.has("added 1") && ruleSnapshot(b) == rules && ks(b, {"checkpoint", "list"}).out == checkpoints, "3 추가 1, 기록은 그대로", pulled.out);
    check(ks(b, {"remote", "list", "rule"}).has("RULE-004"), "4 받아 둔 목록");
    check(ks(b, {"remote", "body", "rule", "RULE-004"}).out == ks(a, {"item", "body", "RULE-004"}).out, "4 받아 둔 본문");
}

fs::path pulledChange(const Remote& remote)
{
    const fs::path a = makeRulesProject(remote, "A");
    must(user(a, {"remote", "push", "rule"}), "A push");
    const fs::path b = makeEmptyProject(remote, "B");
    pullAndAccept(b);
    changeRules(a);
    must(user(a, {"remote", "push", "rule"}), "A push 2");
    must(ks(b, {"remote", "pull", "rule"}), "B pull");
    return b;
}

// ST-041: REQ-043 받아 둔 수칙을 지금 수칙과 비교한다.
void st041()
{
    const Remote remote = makeRemote("st041");
    const fs::path b = pulledChange(remote);
    const Run diff = ks(b, {"remote", "diff", "rule"});
    check(diff.has("RULE-004\tadded") && diff.has("RULE-003\tremoved") && diff.has("RULE-002\tmodified\ttitle"), "1 추가 · 삭제 · 제목 변경", diff.out);
    const Run body = ks(b, {"remote", "diff", "rule", "RULE-001", "--body"});
    check(body.has(" a\n-b\n+B\n c\n"), "2 바뀐 줄만 - · +", body.out);
    must(user(b, {"remote", "discard", "rule"}), "discard");
    refused(ks(b, {"remote", "diff", "rule"}), "받아 둔 수칙이 없음", "3 받아 둔 수칙이 없으면");
}

std::string tagsOf(const fs::path& root, const std::string& id) { return ks(root, {"item", "show", id, "--tags"}).out; }
std::string relationsOf(const fs::path& root, const std::string& id) { return ks(root, {"item", "show", id, "--relations"}).out; }

// ST-042: REQ-044 받아 둔 수칙을 새 체크포인트로 편입한다. (C) 경우는 kiotty_system_test 의 ST-042.
void st042()
{
    const Remote remote = makeRemote("st042");
    const fs::path a = makeRulesProject(remote, "A");
    must(user(a, {"remote", "push", "rule"}), "A push");
    const fs::path b = makeEmptyProject(remote, "B");
    must(ks(b, {"remote", "pull", "rule"}), "B pull");
    start(b, 3);
    refused(user(b, {"remote", "accept", "rule"}), "DONE 이 아님", "1 진행 중이면");
    complete(b);
    must(user(b, {"remote", "accept", "rule"}), "B accept");
    complete(b);
    for (const char* title : {"둘째 임시", "둘째 수칙"}) {
        start(a, 2);
        must(ks(a, {"item", "set", "RULE-002", "--title", title}), "A 제목");
        complete(a);
        must(user(a, {"remote", "push", "rule"}), "A push");
    }
    const Run same = must(ks(b, {"remote", "pull", "rule"}), "B pull 같은 수칙");
    check(same.has("added 0	removed 0	modified 0"), "2 commit 은 다르고 수칙은 같다", same.out);
    refused(user(b, {"remote", "accept", "rule"}), "로컬과 같음", "2 바뀐 것이 없으면");
    must(user(b, {"remote", "discard", "rule"}), "discard");

    start(b, 10);
    int number = 1;
    for (const char* rule : {"RULE-001", "RULE-002", "RULE-003"}) {
        const std::string id = "CIR-00" + std::to_string(number++);
        addItem(b, "code_inspection_result", id, "검사", {"--tag", "결과=PASS"});
        relate(b, id, "검사 개발 수칙", rule);
    }
    complete(b);
    start(a, 2);
    must(ks(a, {"item", "set", "RULE-001", "--body", "a\nB\nc"}), "A RULE-001 변경");
    addItem(a, "rule", "RULE-004", "넷째 수칙", {"--body", "new"});
    complete(a);
    must(user(a, {"remote", "push", "rule"}), "A push 2");
    must(ks(b, {"remote", "pull", "rule"}), "B pull 2");
    const Run accepted = must(user(b, {"remote", "accept", "rule"}), "4 accept");
    const std::string checkpoint = ks(b, {"checkpoint", "show"}).out;
    check(checkpoint.find("1 변경 요청 접수\trunning") != std::string::npos, "4 Step 1 진행 중", checkpoint);
    const std::vector<std::string> lines = linesOf(accepted.out);
    const std::string request = lines.size() > 1 ? lines[1].substr(lines[1].find('\t') + 1) : std::string();
    const std::string tags = tagsOf(b, request);
    check(tags.find("공통 수칙") != std::string::npos && tags.find("열림") != std::string::npos && tags.find("Step 10") != std::string::npos,
          "4 변경 요청의 출처 · 상태 · 진입 단계", tags);
    check(relationsOf(b, "RULE-001").find("CIR-001") == std::string::npos && ks(b, {"relation", "list", "RULE-001"}).out.find("CIR") == std::string::npos,
          "4 변경된 수칙의 검사 결과 관계가 없다");
    check(ks(b, {"remote", "check", "rule"}).out.rfind("same", 0) == 0, "4 기준 버전 = 받은 commit");

    const Remote second = makeRemote("st042_private");
    const fs::path c = makeRulesProject(second, "A");
    must(user(c, {"remote", "push", "rule"}), "A push");
    const fs::path d = makeEmptyProject(second, "B");
    pullAndAccept(d);
    start(d, 2);
    addItem(d, "private_rule", "PRULE-001", "p1");
    addItem(d, "private_rule", "PRULE-002", "p2");
    relate(d, "PRULE-001", "대체 개발 수칙", "RULE-003");
    relate(d, "PRULE-002", "대체 개발 수칙", "RULE-001");
    complete(d);
    changeRules(c);
    must(user(c, {"remote", "push", "rule"}), "A push 2");
    must(ks(d, {"remote", "pull", "rule"}), "B pull 2");
    const Run replaced = must(user(d, {"remote", "accept", "rule"}), "5 accept");
    const std::vector<std::string> replaced_lines = linesOf(replaced.out);
    const std::string replaced_request = replaced_lines.size() > 1 ? replaced_lines[1].substr(replaced_lines[1].find('\t') + 1) : std::string();
    check(relationsOf(d, "PRULE-001").find("RULE-003") == std::string::npos && relationsOf(d, "PRULE-002").find("RULE-001") != std::string::npos,
          "5 지워진 수칙의 대체만 끊긴다");
    const std::string affected = relationsOf(d, replaced_request);
    check(affected.find("PRULE-001") != std::string::npos && affected.find("PRULE-002") != std::string::npos, "5 변경 요청이 두 전용 수칙에 연결", affected);
    const std::string body = ks(d, {"item", "body", replaced_request}).out;
    check(countOf(body, "미정") == 2, "5 처리 표에 미정 행 둘", body);
}

// ST-043: REQ-045 받아 둔 수칙을 버린다.
void st043()
{
    const Remote remote = makeRemote("st043");
    const fs::path b = pulledChange(remote);
    const std::string rules = ruleSnapshot(b);
    const std::string base = ks(b, {"remote", "check", "rule"}).out;
    const std::string pulled = ks(b, {"remote", "diff", "rule"}).out;
    refused(ks(b, {"remote", "discard", "rule"}), "사용자만", "2 AI 의 discard");
    must(ks(b, {"remote", "list", "rule"}), "2 받아 둔 수칙이 남는다");
    must(user(b, {"remote", "discard", "rule"}), "1 discard");
    refused(ks(b, {"remote", "list", "rule"}), "받아 둔 수칙이 없음", "1 discard 뒤");
    check(ruleSnapshot(b) == rules && ks(b, {"remote", "check", "rule"}).out == base, "1 수칙과 기준 버전은 그대로");
    must(ks(b, {"remote", "pull", "rule"}), "1 다시 pull");
    check(ks(b, {"remote", "diff", "rule"}).out == pulled, "1 다시 받으면 같은 차이");
}

// ST-044: REQ-046 이 프로젝트에만 유효한 전용 개발 수칙을 둔다.
void st044()
{
    const fs::path root = makeProject(fresh("st044"));
    start(root, 2);
    addItem(root, "rule", "RULE-001", "대체되는 수칙");
    addItem(root, "rule", "RULE-002", "수칙");
    addItem(root, "private_rule", "PRULE-001", "전용 수칙");
    relate(root, "PRULE-001", "대체 개발 수칙", "RULE-001");
    complete(root);
    start(root, 10);
    addItem(root, "code_inspection_result", "CIR-001", "검사", {"--tag", "결과=PASS"});
    relate(root, "CIR-001", "검사 전용 개발 수칙", "PRULE-001");
    addItem(root, "code_inspection_result", "CIR-002", "검사", {"--tag", "결과=PASS"});
    relate(root, "CIR-002", "검사 개발 수칙", "RULE-002");
    must(user(root, {"checkpoint", "complete"}), "1 대체된 수칙은 검사 결과 없이 확정");
    start(root, 2);
    addItem(root, "private_rule", "PRULE-002", "새 전용 수칙");
    complete(root);
    start(root, 10);
    refused(user(root, {"checkpoint", "complete"}), "PRULE-002", "2 검사 결과 없는 전용 수칙");
    check(ks(root, {"next"}).has("PRULE-002\tprivate_rule\t검사 결과 없음"), "2 ks next 에도 보인다");

    const Remote remote = makeRemote("st044_remote");
    const fs::path a = makeProject(remote.directory / "A");
    start(a, 2);
    addItem(a, "rule", "RULE-076", "공유 수칙", {"--body", "shared"});
    complete(a);
    must(ks(a, {"remote", "open", remote.url}), "A open");
    must(user(a, {"remote", "push", "rule"}), "A push");
    const fs::path b = makeProject(remote.directory / "B");
    start(b, 2);
    addItem(b, "private_rule", "PRULE-001", "p1", {"--body", "private body"});
    complete(b);
    const std::string before = ks(b, {"item", "show", "PRULE-001", "--tags", "--relations"}).out + ks(b, {"item", "body", "PRULE-001"}).out;
    must(ks(b, {"remote", "open", remote.url}), "B open");
    pullAndAccept(b);
    const std::string after = ks(b, {"item", "show", "PRULE-001", "--tags", "--relations"}).out + ks(b, {"item", "body", "PRULE-001"}).out;
    check(ks(b, {"item", "body", "RULE-076"}).out == "shared\n" || ks(b, {"item", "body", "RULE-076"}).out == "shared", "3 RULE-076 이 생긴다");
    check(withoutColumn(after, 4) == withoutColumn(before, 4), "3 전용 수칙은 그대로", before + "----\n" + after);
}

struct Scenario {
    const char* id;
    const char* title;
    void (*run)();
};

const Scenario SCENARIOS[] = {
    {"ST-032", "지금 할 일을 짧게 본다 (ks next)", st032},
    {"ST-033", "ks 의 목적과 작업 방법을 읽는다 (ks guide)", st033},
    {"ST-034", "단계 · Kind 별 안내를 읽는다 (ks guide step · kind)", st034},
    {"ST-035", "처음 마주치는 곳에서 안내를 알린다 (help · init)", st035},
    {"ST-036", "ks next 만 따라 Step 1 에서 Step 2 까지 간다", st036},
    {"ST-037", "개발 수칙을 원격으로 나누는 흐름", st037},
    {"ST-038", "원격 주소를 기억하고 잊는다 (open · close)", st038},
    {"ST-039", "확정된 개발 수칙만 올린다 (push)", st039},
    {"ST-040", "원격 변경을 확인하고 받아 둔다 (check · pull · list · body)", st040},
    {"ST-041", "받아 둔 수칙과 비교한다 (diff)", st041},
    {"ST-042", "받아 둔 수칙을 편입한다 (accept)", st042},
    {"ST-043", "받아 둔 수칙을 버린다 (discard)", st043},
    {"ST-044", "전용 개발 수칙을 둔다 (private_rule)", st044},
};

void setVariable(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value[0] == '\0') {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: kiotty_ks_scenario_test <ks> [ST-xxx]\n");
        return 2;
    }
    std::error_code error {};
    g_ks = pathToUtf8(fs::absolute(fs::u8path(argv[1]), error));
    g_base = fs::temp_directory_path(error) / "kiotty_ks_scenario_test";
    fs::remove_all(g_base, error);
    for (const char* name : {"KIOTTY_SPEC_ACTOR", "KIOTTY_SPEC_ROOT"}) { setVariable(name, ""); }
    setVariable("GIT_AUTHOR_NAME", "kiotty");
    setVariable("GIT_AUTHOR_EMAIL", "kiotty@example.com");
    setVariable("GIT_COMMITTER_NAME", "kiotty");
    setVariable("GIT_COMMITTER_EMAIL", "kiotty@example.com");
    setVariable("GIT_TERMINAL_PROMPT", "0");
    int failed = 0;
    for (const Scenario& scenario : SCENARIOS) {
        if (argc > 2 && std::strcmp(argv[2], scenario.id) != 0) { continue; }
        g_passed = true;
        scenario.run();
        std::printf("%s %s %s\n", scenario.id, g_passed ? "PASS" : "FAIL", scenario.title);
        std::fflush(stdout);
        if (!g_passed) { ++failed; }
    }
    fs::remove_all(g_base, error);
    std::printf("%d failed\n", failed);
    return failed;
}
