// 시스템 테스트 (Step 8 의 ST-001 ~ ST-031). 기능 요구사항의 인수 조건을 C 인터페이스로만 실행한다.
// 시스템 테스트마다 새 임시 폴더에 사양 파일을 연다. 결과 줄은 "ST-### PASS|FAIL 제목" 이다.

#include "kiotty_spec.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr KiottySpecItemKind REQUIREMENT = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
constexpr KiottySpecItemKind DESIGN = KIOTTY_SPEC_ITEM_KIND_DESIGN;
constexpr KiottySpecItemKind CHANGE_REQUEST = KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST;
constexpr KiottySpecText UNCHANGED {nullptr, 0};
const char* const CRITERIA = "## 인수 조건\n- 전제 조건: a / 행동: b / 기대 결과: c\n";

bool g_passed = true;

void check(bool condition, const std::string& label)
{
    if (condition) { return; }
    std::printf("  FAIL: %s\n", label.c_str());
    g_passed = false;
}

KiottySpecText textOf(const std::string& text) { return KiottySpecText{text.data(), text.size()}; }
bool ok(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }
uint32_t sequenceOf(KiottySpecCheckpointHandle handle) { return static_cast<uint32_t>(handle & 0xFFFFFFFFu); }

KiottySpecIdResult createItem(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText id, KiottySpecText title, KiottySpecText summary)
{
    KiottySpecItemDraft draft {};
    draft.id = id;
    draft.title = title;
    draft.summary = summary;
    draft.kind = kind;
    return KiottySpec_createItem(context, &draft);
}

KiottySpecDiffSummaryResult summarizeDiff(KiottySpecContext* context, const KiottySpecDiffQuery& query) { return KiottySpec_summarizeDiff(context, &query); }
KiottySpecCursorResult openItemDiffCursor(KiottySpecContext* context, const KiottySpecDiffQuery& query) { return KiottySpec_openItemDiffCursor(context, &query); }
KiottySpecCursorResult openRelationDiffCursor(KiottySpecContext* context, const KiottySpecDiffQuery& query)
{
    return KiottySpec_openRelationDiffCursor(context, &query);
}

KiottySpecTagQuery tagQuery(KiottySpecItemKind kind, uint32_t tag_index, uint64_t mask)
{
    KiottySpecTagQuery query {};
    query.mask = mask;
    query.kind = kind;
    query.tag_index = tag_index;
    return query;
}

std::string readAll(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

void writeAll(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

// 컨텍스트 하나와 그 사양 파일. 소멸자가 컨텍스트를 해제한다.
class Spec
{
public:
    explicit Spec(const fs::path& root, KiottySpecConfiguration configuration = {}) : root(root)
    {
        context = KiottySpec_createContext(&configuration).value;
        opened = KiottySpec_open(context, textOf(file().generic_string())).code;
    }
    ~Spec() { KiottySpec_releaseContext(context); }
    Spec(const Spec&) = delete;
    Spec& operator=(const Spec&) = delete;

    fs::path file() const { return root / ".kiotty" / "spec.kisp"; }

    KiottySpecError lastError() { return KiottySpec_readLastError(context).value; }

    // "상태/주제/줄/글" 꼴. 실패를 찍을 때 쓴다.
    std::string error()
    {
        const KiottySpecError detail = lastError();
        return std::string(KiottySpec_formatStatus(detail.status).value.value) + "/" + std::to_string(static_cast<int>(detail.subject)) + "/" +
               std::to_string(detail.line) + "/" + detail.text;
    }

    bool failedWith(KiottySpecSubject subject, const std::string& text)
    {
        const KiottySpecError detail = lastError();
        return detail.subject == subject && text == detail.text;
    }

    KiottySpecCheckpointHandle start(KiottySpecStep step) { return KiottySpec_startCheckpoint(context, step, textOf("시험")).value; }
    KiottySpecStatus complete() { return KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code; }

    std::string add(KiottySpecItemKind kind, const std::string& id = "", const std::string& title = "제목", const std::string& summary = "")
    {
        const KiottySpecIdResult created = createItem(context, kind, textOf(id), textOf(title), textOf(summary));
        return ok(created.code) ? std::string(created.value) : std::string();
    }

    KiottySpecItemHandle handle(const std::string& id) { return KiottySpec_findItem(context, textOf(id)).value; }
    KiottySpecItem item(const std::string& id) { return KiottySpec_readItem(context, textOf(id)).value; }
    KiottySpecStatus body(const std::string& id, const std::string& text) { return KiottySpec_writeItemBody(context, textOf(id), textOf(text)).code; }

    std::string readBody(const std::string& id)
    {
        std::vector<char> buffer(KIOTTY_SPEC_MAX_BODY_LENGTH + 1);
        KiottySpec_readItemBody(context, textOf(id), KiottySpecBuffer{buffer.data(), buffer.size()});
        return buffer.data();
    }

    std::vector<KiottySpecTagDefinition> tagDefinitions(KiottySpecItemKind kind)
    {
        std::vector<KiottySpecTagDefinition> definitions {};
        const KiottySpecCursorResult cursor = KiottySpec_openTagDefinitionCursor(context, kind);
        const std::unique_ptr<KiottySpecTagDefinitionPage> page = std::make_unique<KiottySpecTagDefinitionPage>();
        const KiottySpecStatus fetched = KiottySpec_fetchTagDefinitionPage(context, cursor.value, page.get()).code;
        for (size_t index = 0; fetched == KIOTTY_SPEC_STATUS_SUCCESS && index < page->length; ++index) {
            definitions.push_back(page->elements[index]);
        }
        KiottySpec_closeCursor(context, cursor.value);
        return definitions;
    }

    std::vector<KiottySpecRelationDefinition> relationDefinitions(KiottySpecItemKind kind)
    {
        std::vector<KiottySpecRelationDefinition> definitions {};
        const KiottySpecCursorResult cursor = KiottySpec_openRelationDefinitionCursor(context, kind);
        const std::unique_ptr<KiottySpecRelationDefinitionPage> page = std::make_unique<KiottySpecRelationDefinitionPage>();
        const KiottySpecStatus fetched = KiottySpec_fetchRelationDefinitionPage(context, cursor.value, page.get()).code;
        for (size_t index = 0; fetched == KIOTTY_SPEC_STATUS_SUCCESS && index < page->length; ++index) {
            definitions.push_back(page->elements[index]);
        }
        KiottySpec_closeCursor(context, cursor.value);
        return definitions;
    }

    KiottySpecTagDefinition findTagDefinition(KiottySpecItemKind kind, const std::string& name)
    {
        const std::vector<KiottySpecTagDefinition> definitions = tagDefinitions(kind);
        for (const KiottySpecTagDefinition& definition : definitions) {
            if (name == definition.name) { return definition; }
        }
        KiottySpecTagDefinition missing {};
        missing.index = 0xFFFFFFFFu;
        return missing;
    }

    uint32_t tag(KiottySpecItemKind kind, const std::string& name) { return findTagDefinition(kind, name).index; }

    uint64_t bit(KiottySpecItemKind kind, const std::string& tag_name, const std::string& value)
    {
        const KiottySpecTagDefinition definition = findTagDefinition(kind, tag_name);
        for (uint32_t index = 0; index < KIOTTY_SPEC_TAG_BIT_COUNT; ++index) {
            if (value == definition.bit_names[index]) { return uint64_t{1} << index; }
        }
        return 0;
    }

    KiottySpecStatus setTag(const std::string& id, KiottySpecItemKind kind, const std::string& tag_name, const std::string& value)
    {
        return KiottySpec_setItemTag(context, handle(id), tag(kind, tag_name), bit(kind, tag_name, value)).code;
    }

    uint32_t relation(KiottySpecItemKind kind, const std::string& name)
    {
        const std::vector<KiottySpecRelationDefinition> definitions = relationDefinitions(kind);
        for (const KiottySpecRelationDefinition& definition : definitions) {
            if (name == definition.name) { return definition.index; }
        }
        return 0xFFFFFFFFu;
    }

    KiottySpecStatus link(const std::string& from, KiottySpecItemKind kind, const std::string& name, const std::string& to)
    {
        return KiottySpec_addRelation(context, textOf(from), relation(kind, name), textOf(to)).code;
    }

    // 기능 요구사항 하나 (인수 조건 포함)
    std::string functional(const std::string& id = "")
    {
        const std::string made = add(REQUIREMENT, id, "기능");
        setTag(made, REQUIREMENT, "요구사항 종류", "기능 요구사항");
        body(made, CRITERIA);
        return made;
    }

    std::vector<std::string> ids(KiottySpecCursorResult cursor)
    {
        std::vector<std::string> result;
        KiottySpecItemPage page {};
        for (KiottySpec_fetchItemPage(context, cursor.value, &page); page.length > 0; KiottySpec_fetchItemPage(context, cursor.value, &page)) {
            for (size_t index = 0; index < page.length; ++index) { result.push_back(page.elements[index].id); }
        }
        KiottySpec_closeCursor(context, cursor.value);
        return result;
    }

    std::vector<std::string> impact(const std::string& id, uint32_t depth)
    {
        const KiottySpecCursorResult cursor = KiottySpec_openImpactCursor(context, textOf(id), depth);
        std::vector<std::string> result;
        KiottySpecIdPage page {};
        for (KiottySpec_fetchIdPage(context, cursor.value, &page); page.length > 0; KiottySpec_fetchIdPage(context, cursor.value, &page)) {
            for (size_t index = 0; index < page.length; ++index) { result.push_back(page.elements[index]); }
        }
        KiottySpec_closeCursor(context, cursor.value);
        return result;
    }

    KiottySpecPosition position() { return KiottySpec_readPosition(context).value; }

    fs::path root;
    KiottySpecContext* context {nullptr};
    KiottySpecStatus opened {KIOTTY_SPEC_STATUS_SUCCESS};
};

fs::path g_base;

fs::path freshRoot(const std::string& name)
{
    std::error_code error;
    const fs::path root = g_base / name;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    return root;
}

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    for (const std::string& each : values) {
        if (each == value) { return true; }
    }
    return false;
}

int run(const std::string& command) { return std::system(command.c_str()); }

std::string quoted(const fs::path& path) { return "\"" + path.generic_string() + "\""; }

// ---------------------------------------------------------------- REQ-001 ~ 006

void st001(const fs::path& root)
{
    {
        Spec spec(root / "project");
        check(ok(spec.opened) && fs::is_regular_file(spec.file()), "1 없는 사양 파일을 열면 파일이 생긴다");
        const KiottySpecCursorResult items = KiottySpec_openItemCursor(spec.context, static_cast<KiottySpecItemKind>(0), nullptr, 0);
        check(items.total == 0 && spec.position().current == 0, "1 항목 · 체크포인트가 없다");
        KiottySpec_closeCursor(spec.context, items.value);
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        spec.add(REQUIREMENT, "REQ-001", "옮기기 전");
    }
    std::error_code error;
    fs::rename(root / "project", root / "moved", error);
    Spec moved(root / "moved");
    check(ok(moved.opened) && std::string(moved.item("REQ-001").title) == "옮기기 전", "2 옮긴 폴더에서 같은 기록");
}

void st002(const fs::path& root)
{
    const fs::path nested = root / "a" / "b";
    Spec spec(nested);
    check(ok(spec.opened) && fs::is_regular_file(spec.file()), "1 없는 경로는 상위 폴더와 함께 만들어진다");
    check(KiottySpec_open(spec.context, textOf((root / "other.kisp").generic_string())).code == KIOTTY_SPEC_STATUS_PRECONDITION, "2 이미 연 컨텍스트에서 다시 열기 거부");
    KiottySpecConfiguration configuration {};
    KiottySpecContext* folder = KiottySpec_createContext(&configuration).value;
    check(KiottySpec_open(folder, textOf(root.generic_string())).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "3 폴더 경로 거부");
    KiottySpec_releaseContext(folder);
    KiottySpecContext* empty = KiottySpec_createContext(&configuration).value;
    check(KiottySpec_open(empty, textOf("")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "4 빈 경로 거부");
    KiottySpec_releaseContext(empty);
}

void st003(const fs::path& root)
{
    const fs::path project = root / "my_project";
    Spec spec(project);
    check(ok(KiottySpec_initializeProject(spec.context, textOf("")).code), "1 체크포인트 없이 초기화");
    const std::string text = readAll(spec.file());
    check(text.find("meta\tO\tproject.name\tmy_project\n") != std::string::npos, "1 이름은 루트 폴더 이름");
    const size_t created = text.find("meta\tO\tproject.created_at\t");
    check(created != std::string::npos && text.compare(created + 26, 2, "0\n") != 0, "1 만든 시각이 기록된다");
    KiottySpec_initializeProject(spec.context, textOf("다른 이름"));
    check(readAll(spec.file()).find("meta\tO\tproject.name\t다른 이름\n") != std::string::npos, "2 준 이름이 기록된다");
}

// 두 브랜치가 서로 다른 항목을 더한 뒤 병합한다.
void st004Merge(const fs::path& root)
{
    const fs::path repository = root / "repository";
    {
        Spec spec(repository);
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        for (int index = 0; index < 50; ++index) { spec.add(REQUIREMENT); }
    }
    const std::string git = "git -C " + quoted(repository) + " -c user.name=t -c user.email=t@t -c core.autocrlf=false ";
    bool ran = run(git + "init -q") == 0 && run(git + "add -A") == 0 && run(git + "commit -q -m base") == 0 && run(git + "branch side") == 0;
    {
        Spec main(repository);
        main.add(REQUIREMENT, "REQ-101", "메인");
    }
    ran = ran && run(git + "commit -q -am main") == 0 && run(git + "checkout -q side") == 0;
    {
        Spec side(repository);
        side.add(REQUIREMENT, "REQ-202", "옆");
    }
    ran = ran && run(git + "commit -q -am side") == 0 && run(git + "checkout -q -") == 0;
    const bool merged = ran && run(git + "merge -q side -m merge") == 0;
    if (!merged) { run(git + "merge --abort"); }
    Spec result(repository);
    const bool both = ok(result.opened) && std::string(result.item("REQ-101").title) == "메인" && std::string(result.item("REQ-202").title) == "옆";
    check(ran && merged && both, "1 서로 다른 항목을 더한 두 브랜치가 충돌 없이 병합되고 둘 다 보인다");
}

void st004(const fs::path& root)
{
    st004Merge(root);
    const fs::path marked = root / "marked";
    {
        Spec spec(marked);
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        spec.add(REQUIREMENT);
    }
    writeAll(marked / ".kiotty" / "spec.kisp", readAll(marked / ".kiotty" / "spec.kisp") + "<<<<<<< HEAD\n");
    Spec conflict(marked);
    const KiottySpecError marker = conflict.lastError();
    check(conflict.opened == KIOTTY_SPEC_STATUS_CONFLICT && marker.subject == KIOTTY_SPEC_SUBJECT_MERGE_MARKER && marker.line > 0,
          "2 충돌 표시가 남은 파일은 줄 번호와 함께 거부");

    const fs::path doubled = root / "doubled";
    std::string text;
    {
        Spec spec(doubled);
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        spec.add(REQUIREMENT);
        text = readAll(spec.file());
    }
    const size_t line = text.find("\nitem_version\t");
    const size_t end = text.find('\n', line + 1);
    writeAll(doubled / ".kiotty" / "spec.kisp", text.substr(0, end + 1) + text.substr(line + 1, end - line) + text.substr(end + 1));
    Spec duplicate(doubled);
    check(duplicate.opened == KIOTTY_SPEC_STATUS_CONFLICT, "3 같은 레코드 버전이 두 줄인 파일은 거부");

    const fs::path same = root / "same";
    {
        Spec spec(same);
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        spec.add(REQUIREMENT);
    }
    const std::string before = readAll(same / ".kiotty" / "spec.kisp");
    {
        Spec spec(same);
        KiottySpec_resumeCheckpoint(spec.context);
    }
    check(readAll(same / ".kiotty" / "spec.kisp") == before, "4 바꾸지 않은 쓰기는 파일을 바이트까지 그대로 둔다");
}

void st005(const fs::path& root)
{
    Spec first(root);
    first.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    Spec second(root);
    first.add(REQUIREMENT, "REQ-001", "앞");
    const KiottySpecIdResult late = createItem(second.context, REQUIREMENT, textOf("REQ-002"), textOf("뒤"), textOf(""));
    check(late.code == KIOTTY_SPEC_STATUS_CONFLICT, "1 나중 쓰기는 충돌로 거부");
    check(ok(KiottySpec_findItem(second.context, textOf("REQ-001")).code), "1 거부된 쪽에 앞쪽 변경이 보인다");
    check(!second.add(REQUIREMENT, "REQ-002", "뒤").empty(), "2 다시 하면 성공");
}

void st006(const fs::path& root)
{
    writeAll(root / "version" / ".kiotty" / "spec.kisp", "#kisp 1 model 1\n");
    Spec version(root / "version");
    check(version.opened == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && version.lastError().subject == KIOTTY_SPEC_SUBJECT_FILE_FORMAT && version.lastError().line == 1,
          "1 형식 버전이 다른 파일은 첫 줄의 형식 오류로 거부");
    writeAll(root / "tag" / ".kiotty" / "spec.kisp",
             "#kisp 2 model 1\nitem\tO\t0000abcd\tREQ-001\nitem_version\tO\t0000abcd\t1\trequirement\t-\t제목\t\t-\ntag\tO\t0000abcd\t없는 태그\t1\t기능 요구사항\n");
    Spec tag(root / "tag");
    check(tag.opened == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && tag.lastError().line == 4 && tag.failedWith(KIOTTY_SPEC_SUBJECT_TAG, "없는 태그"),
          "2 모델에 없는 태그는 줄 번호와 이름으로 거부");
    writeAll(root / "escape" / ".kiotty" / "spec.kisp", "#kisp 2 model 1\nmeta\tO\tproject.name\ta\\qb\n");
    Spec escape(root / "escape");
    check(escape.opened == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && escape.lastError().line == 2 && escape.lastError().subject == KIOTTY_SPEC_SUBJECT_FILE_LINE,
          "3 잘못된 이스케이프는 줄 번호로 거부");
}

// ---------------------------------------------------------------- REQ-007 ~ 013

void st007(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const std::string first = spec.add(REQUIREMENT);
    const std::string second = spec.add(REQUIREMENT);
    const std::vector<std::string> listed = spec.ids(KiottySpec_openItemCursor(spec.context, REQUIREMENT, nullptr, 0));
    check(first == "REQ-001" && second == "REQ-002" && listed.size() == 2, "1 REQ-001, REQ-002 가 붙고 목록에 둘");
    spec.body("REQ-001", "## 진술\n저장한다\n");
    check(spec.readBody("REQ-001") == "## 진술\n저장한다\n", "2 쓴 본문 그대로 읽힌다");
}

void st008(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    for (int index = 0; index < 6; ++index) { spec.add(DESIGN); }
    KiottySpec_removeItem(spec.context, textOf("DSN-006"));
    check(spec.add(DESIGN) == "DSN-007", "1 지운 DSN-006 까지 세어 DSN-007");
    check(createItem(spec.context, DESIGN, textOf("DSN-006"), textOf("t"), UNCHANGED).code == KIOTTY_SPEC_STATUS_CONFLICT, "2 지운 항목의 ID 거부");
    // 상태 코드는 요구사항이 정하지 않는다. 거부되고 이유가 ID 형식이면 된다 (16 자는 CAPACITY_EXCEEDED).
    const bool spaced = !ok(createItem(spec.context, DESIGN, textOf("DSN 1"), textOf("t"), UNCHANGED).code) && spec.failedWith(KIOTTY_SPEC_SUBJECT_ID, "DSN 1");
    const bool long_id = !ok(createItem(spec.context, DESIGN, textOf("DSN-123456789012"), textOf("t"), UNCHANGED).code) &&
                         spec.failedWith(KIOTTY_SPEC_SUBJECT_ID, "DSN-123456789012");
    check(spaced && long_id, "3 공백이나 16 자 이상의 ID 는 ID 형식이 잘못되었다는 이유로 거부");
    check(createItem(spec.context, DESIGN, textOf(""), textOf(""), UNCHANGED).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "4 빈 제목 거부");
}

void st009(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.add(REQUIREMENT, "REQ-001", "제목", "요약");
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), UNCHANGED, textOf("새 요약"));
    const KiottySpecItem item = spec.item("REQ-001");
    check(std::string(item.summary) == "새 요약" && std::string(item.title) == "제목", "1 요약만 바뀐다");
    check(KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf(""), UNCHANGED).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 빈 제목 거부");
    check(spec.body("REQ-001", std::string(KIOTTY_SPEC_MAX_BODY_LENGTH + 1, 'a')) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "3 16384 바이트를 넘는 본문 거부");
    check(KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("\xff\xfe"), UNCHANGED).code == KIOTTY_SPEC_STATUS_INVALID_UTF8, "4 UTF-8 이 아닌 제목 거부");
    spec.body("REQ-001", "0123456789");
    char small[4] {};
    const KiottySpecSizeResult read = KiottySpec_readItemBody(spec.context, textOf("REQ-001"), KiottySpecBuffer{small, sizeof(small)});
    check(read.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED && read.value == 11, "5 작은 버퍼는 필요한 바이트 수를 알려 준다");
    spec.body("REQ-001", "");
    check(spec.readBody("REQ-001").empty(), "6 빈 본문을 쓰면 본문이 없다");
}

void st010(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    const std::string a = spec.add(KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE);
    const std::string b = spec.add(KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE);
    KiottySpec_setItemParent(spec.context, textOf(b), textOf(a));
    const std::vector<std::string> children = spec.ids(KiottySpec_openChildCursor(spec.context, textOf(a)));
    check(children.size() == 1 && children[0] == b, "1 A 의 하위는 B");
    check(KiottySpec_setItemParent(spec.context, textOf(a), textOf(b)).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 순환하는 상위 거부");
    KiottySpec_setItemParent(spec.context, textOf(b), textOf(""));
    check(spec.item(b).parent == KIOTTY_SPEC_INVALID_ITEM_HANDLE, "3 상위를 없애면 최상위");
}

void st011(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle before = spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.functional("REQ-001");
    spec.add(DESIGN, "DSN-001");
    spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-001");
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    KiottySpec_removeItem(spec.context, textOf("DSN-001"));
    const std::vector<std::string> left = spec.ids(KiottySpec_openItemCursor(spec.context, DESIGN, nullptr, 0));
    const KiottySpecCursorResult relations = KiottySpec_openRelationCursor(spec.context, textOf("REQ-001"), KIOTTY_SPEC_DIRECTION_BOTH);
    KiottySpec_closeCursor(spec.context, relations.value);
    const bool past = ok(KiottySpec_readItemAt(spec.context, textOf("DSN-001"), before).code);
    check(left.empty() && relations.total == 0 && past, "1 목록 · 관계에서 사라지고 지우기 전 시점에는 보인다");
    spec.add(DESIGN, "DSN-010");
    spec.add(DESIGN, "DSN-011");
    KiottySpec_setItemParent(spec.context, textOf("DSN-011"), textOf("DSN-010"));
    const KiottySpecStatus removed = KiottySpec_removeItem(spec.context, textOf("DSN-010")).code;
    check(removed == KIOTTY_SPEC_STATUS_PRECONDITION && spec.failedWith(KIOTTY_SPEC_SUBJECT_CHILD, "DSN-011"), "2 하위가 있으면 하위 ID 와 함께 거부");
}

void st012(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.functional("REQ-005");
    spec.add(DESIGN, "DSN-001");
    spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-005");
    check(ok(KiottySpec_renameItem(spec.context, textOf("REQ-005"), textOf("REQ-050")).code), "1 ID 를 바꾼다");
    const KiottySpecCursorResult relations = KiottySpec_openRelationCursor(spec.context, textOf("REQ-050"), KIOTTY_SPEC_DIRECTION_DOWNSTREAM);
    KiottySpec_closeCursor(spec.context, relations.value);
    const bool gone = KiottySpec_findItem(spec.context, textOf("REQ-005")).code == KIOTTY_SPEC_STATUS_NOT_FOUND;
    check(relations.total == 1 && spec.readBody("REQ-050") == CRITERIA && gone, "1 새 ID 로 같은 관계 · 본문, 옛 ID 는 없다");
    spec.add(REQUIREMENT, "REQ-001");
    spec.add(REQUIREMENT, "REQ-002");
    check(KiottySpec_renameItem(spec.context, textOf("REQ-001"), textOf("REQ-002")).code == KIOTTY_SPEC_STATUS_CONFLICT, "2 있는 ID 로 바꾸기 거부");
}

void st013(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    for (int index = 0; index < 3; ++index) { spec.add(DESIGN); }
    spec.setTag("DSN-002", DESIGN, "구현 상태", "변경 필요");
    const KiottySpecTagQuery query = tagQuery(DESIGN, spec.tag(DESIGN, "구현 상태"), spec.bit(DESIGN, "구현 상태", "변경 필요"));
    const std::vector<std::string> found = spec.ids(KiottySpec_openItemCursor(spec.context, DESIGN, &query, 1));
    check(found.size() == 1 && found[0] == "DSN-002", "1 구현 상태가 변경 필요인 설계 사항 하나");
    for (int index = 0; index < 100; ++index) { spec.add(REQUIREMENT); }
    const KiottySpecCursorResult cursor = KiottySpec_openItemCursor(spec.context, REQUIREMENT, nullptr, 0);
    KiottySpecItemPage page {};
    KiottySpec_fetchItemPage(spec.context, cursor.value, &page);
    const size_t first = page.length;
    KiottySpec_fetchItemPage(spec.context, cursor.value, &page);
    const size_t second = page.length;
    KiottySpec_closeCursor(spec.context, cursor.value);
    check(cursor.total == 100 && first == 64 && second == 36, "2 전체 100, 페이지 64 개씩");
    check(KiottySpec_readItem(spec.context, textOf("REQ-999")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "3 없는 ID 는 없다는 상태");
}

// ---------------------------------------------------------------- REQ-014 ~ 018

void st014(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.functional("REQ-001");
    spec.add(DESIGN, "DSN-001");
    check(ok(spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-001")), "1 관련 요구사항으로 연결된다");
    check(KiottySpec_addRelation(spec.context, textOf("DSN-001"), 99, textOf("REQ-001")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 모델에 없는 관계 거부");
}

void st015(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED);
    spec.add(REQUIREMENT, "REQ-001");
    spec.setTag("REQ-001", REQUIREMENT, "요구사항 종류", "기능 요구사항");
    check(KiottySpec_readItemTag(spec.context, spec.handle("REQ-001"), 0).value.value == spec.bit(REQUIREMENT, "요구사항 종류", "기능 요구사항"), "1 기능 요구사항으로 읽힌다");
    check(KiottySpec_setItemTag(spec.context, spec.handle("REQ-001"), 1, 1).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 요구사항 모델에 없는 태그 거부");
    spec.add(CHANGE_REQUEST, "CR-001");
    const uint64_t two = spec.bit(CHANGE_REQUEST, "상태", "열림") | spec.bit(CHANGE_REQUEST, "상태", "완료");
    check(KiottySpec_setItemTag(spec.context, spec.handle("CR-001"), spec.tag(CHANGE_REQUEST, "상태"), two).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
          "3 하나만 가질 수 있는 태그에 값 둘 거부");
    KiottySpec_setItemTag(spec.context, spec.handle("REQ-001"), 0, 0);
    check(KiottySpec_readItemTag(spec.context, spec.handle("REQ-001"), 0).value.value == 0, "4 비우면 태그가 없다");
}

void st016(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_UNIT_TEST_PLAN_DISCUSSING);
    spec.functional("REQ-001");
    spec.add(DESIGN, "DSN-001");
    spec.add(KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "UT-001");
    check(ok(spec.link("UT-001", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "검증 설계 사항", "DSN-001")), "1 검증 설계 사항으로 연결");
    const KiottySpecCursorResult down = KiottySpec_openRelationCursor(spec.context, textOf("DSN-001"), KIOTTY_SPEC_DIRECTION_DOWNSTREAM);
    KiottySpec_closeCursor(spec.context, down.value);
    check(down.total == 1, "1 설계 사항의 아래 관계에 그 테스트");
    check(spec.link("UT-001", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "검증 설계 사항", "REQ-001") == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 요구사항에 연결 거부");
    check(spec.link("UT-001", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "검증 설계 사항", "DSN-001") == KIOTTY_SPEC_STATUS_CONFLICT, "3 같은 관계 다시 거부");
    spec.add(DESIGN, "DSN-002");
    check(KiottySpec_removeRelation(spec.context, textOf("UT-001"), 0, textOf("DSN-002")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "4 없는 관계 끊기는 없다는 상태");
    spec.add(KIOTTY_SPEC_ITEM_KIND_MILESTONE, "MS-001");
    spec.add(KIOTTY_SPEC_ITEM_KIND_MILESTONE, "MS-002");
    spec.link("MS-001", KIOTTY_SPEC_ITEM_KIND_MILESTONE, "포함 설계 사항", "DSN-001");
    spec.link("MS-002", KIOTTY_SPEC_ITEM_KIND_MILESTONE, "포함 설계 사항", "DSN-002");
    const KiottySpecCursorResult all = KiottySpec_openRelationCursorByModel(spec.context, KIOTTY_SPEC_ITEM_KIND_MILESTONE, spec.relation(KIOTTY_SPEC_ITEM_KIND_MILESTONE, "포함 설계 사항"));
    KiottySpec_closeCursor(spec.context, all.value);
    check(all.total == 2, "5 포함 설계 사항 관계 전체가 둘");
}

void st017(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecModel model = KiottySpec_readModel(spec.context, DESIGN).value;
    check(std::string(model.name) == "설계 사항" && std::string(model.id_prefix) == "DSN" && model.created_in == KIOTTY_SPEC_STEP_DESIGN_DISCUSSING &&
              model.tag_count == 2 && model.relation_count == 3,
          "1 설계 사항, DSN, 단계 4, 태그 2, 관계 3");
    check(KiottySpec_parseKind(textOf("design")) == DESIGN, "2 design 은 설계 사항");
    check(KiottySpec_parseKind(textOf("nothing")) == 0, "3 모르는 이름은 0");
}

void st018(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_UNIT_TEST_PLAN_DISCUSSING);
    spec.functional("REQ-001");
    spec.add(DESIGN, "DSN-001");
    spec.add(KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "UT-001");
    spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-001");
    spec.link("UT-001", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN, "검증 설계 사항", "DSN-001");
    const std::vector<std::string> one = spec.impact("REQ-001", 1);
    check(one.size() == 1 && one[0] == "DSN-001", "1 깊이 1 은 설계 사항");
    const std::vector<std::string> two = spec.impact("REQ-001", 2);
    check(two.size() == 2 && contains(two, "DSN-001") && contains(two, "UT-001"), "2 깊이 2 는 설계 사항과 유닛 테스트");
}

// ---------------------------------------------------------------- REQ-019 ~ 024

void st019(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.functional("REQ-001");
    check(ok(spec.complete()) && spec.position().next_step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING, "1 확정되고 다음 단계가 Step 3");
    check(KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("고침"), UNCHANGED).code == KIOTTY_SPEC_STATUS_CONFIRMED, "2 확정된 체크포인트에서 고치기 거부");
}

void st020(const fs::path& root)
{
    Spec spec(root);
    check(createItem(spec.context, REQUIREMENT, textOf(""), textOf("t"), UNCHANGED).code == KIOTTY_SPEC_STATUS_PRECONDITION, "1 체크포인트가 없으면 거부");
    check(KiottySpec_startCheckpoint(spec.context, static_cast<KiottySpecStep>(13), textOf("")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 단계 13 거부");
    const KiottySpecCheckpointHandle started = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const bool was_started = KiottySpec_readCheckpoint(spec.context, KIOTTY_SPEC_CHECKPOINT_CURRENT).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_STARTED;
    spec.add(REQUIREMENT, "REQ-001", "한 번");
    const bool running = KiottySpec_readCheckpoint(spec.context, KIOTTY_SPEC_CHECKPOINT_CURRENT).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING;
    check(was_started && running && sequenceOf(spec.item("REQ-001").checkpoint) == sequenceOf(started), "3 시작됨이 진행 중이 되고 항목이 거기에 묶인다");
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("두 번"), UNCHANGED);
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("세 번"), UNCHANGED);
    const std::string text = readAll(spec.file());
    size_t versions = 0;
    for (size_t at = text.find("\nitem_version\t"); at != std::string::npos; at = text.find("\nitem_version\t", at + 1)) { ++versions; }
    check(versions == 1, "4 한 체크포인트에서 세 번 고쳐도 버전 하나");
}

// 확정이 COMPLETION 으로 거부되면 어긋난 조건을 "조건:ID " 로 이어 돌려준다.
std::string completeFailure(Spec& spec)
{
    const bool refused = spec.complete() == KIOTTY_SPEC_STATUS_PRECONDITION && spec.lastError().subject == KIOTTY_SPEC_SUBJECT_COMPLETION;
    if (!refused) { return std::string(); }
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(spec.context);
    std::string rows;
    KiottySpecUnmetConditionPage page {};
    for (size_t fetched = 0; ok(cursor.code) && fetched < cursor.total; fetched += page.length) {
        if (!ok(KiottySpec_fetchUnmetConditionPage(spec.context, cursor.value, &page).code) || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            rows += std::to_string(static_cast<int>(page.elements[index].condition)) + ":" + page.elements[index].id + " ";
        }
    }
    KiottySpec_closeCursor(spec.context, cursor.value);
    return rows;
}

std::string unmet(KiottySpecCondition condition, const std::string& id) { return std::to_string(static_cast<int>(condition)) + ":" + id + " "; }

void st021(const fs::path& root)
{
    {
        Spec spec(root / "actor");
        spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
        spec.add(DESIGN);
        check(KiottySpec_completeCheckpoint(spec.context, KIOTTY_SPEC_ACTOR_AI).code == KIOTTY_SPEC_STATUS_PERMISSION, "1 AI 는 확정할 수 없다");
    }
    {
        Spec spec(root / "step2");
        spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
        spec.add(REQUIREMENT, "REQ-001");
        spec.setTag("REQ-001", REQUIREMENT, "요구사항 종류", "기능 요구사항");
        check(completeFailure(spec).find(unmet(KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA, "REQ-001")) != std::string::npos,
              "2 인수 조건이 없는 기능 요구사항이 있으면 그 ID 와 함께 거부");
    }
    {
        Spec spec(root / "step6");
        spec.start(KIOTTY_SPEC_STEP_DEVELOPMENT_PLAN_DISCUSSING);
        spec.add(DESIGN, "DSN-001");
        check(completeFailure(spec).find(unmet(KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE, "DSN-001")) != std::string::npos,
              "3 마일스톤에 없는 설계 사항이 있으면 그 ID 와 함께 거부");
    }
    {
        Spec spec(root / "step8");
        spec.start(KIOTTY_SPEC_STEP_SYSTEM_TEST_PLAN_DISCUSSING);
        spec.functional("REQ-001");
        spec.add(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN, "ST-001");
        spec.add(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN, "ST-002");
        spec.link("ST-001", KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN, "검증 요구사항", "REQ-001");
        spec.link("ST-002", KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN, "검증 요구사항", "REQ-001");
        check(completeFailure(spec).find(unmet(KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR, "REQ-001")) != std::string::npos,
              "4 시스템 테스트가 둘인 요구사항이 있으면 그 ID 와 함께 거부");
    }
    {
        Spec spec(root / "step12");
        spec.start(KIOTTY_SPEC_STEP_END_TO_END_TEST_RUN);
        spec.add(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_RESULT, "STR-001");
        spec.setTag("STR-001", KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_RESULT, "결과", "FAIL");
        check(completeFailure(spec).find(unmet(KIOTTY_SPEC_CONDITION_PASSING_RESULT, "STR-001")) != std::string::npos, "5 FAIL 인 결과가 있으면 그 ID 와 함께 거부");
    }
    {
        Spec spec(root / "step11");
        spec.start(KIOTTY_SPEC_STEP_END_TO_END_TEST_DISCUSSING);
        spec.add(KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-001");
        spec.add(KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-002");
        spec.add(KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-003");
        KiottySpec_setItemParent(spec.context, textOf("UI-002"), textOf("UI-001"));
        spec.add(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN, "E2E-001");
        spec.link("E2E-001", KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN, "검증 외부 인터페이스", "UI-001");
        const std::string unmet_items = completeFailure(spec);
        check(unmet_items.find(unmet(KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR, "UI-003")) != std::string::npos &&
                  unmet_items.find("UI-002") == std::string::npos,
              "8 E2E 테스트가 없는 묶음(상위 없는 외부 인터페이스)은 그 ID 와 함께 거부, 하위 항목은 짝을 보지 않는다");
        spec.add(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN, "E2E-002");
        spec.link("E2E-002", KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN, "검증 외부 인터페이스", "UI-003");
        check(ok(spec.complete()), "9 묶음마다 E2E 테스트가 하나면 확정된다");
    }
    {
        Spec spec(root / "step4");
        spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
        spec.add(DESIGN);
        check(ok(spec.complete()), "6 조건이 없는 Step 4 는 확정된다");
        check(ok(spec.complete()), "7 확정된 체크포인트를 다시 확정하면 그대로 성공");
    }
}

void st022(const fs::path& root)
{
    {
        Spec spec(root / "none");
        const KiottySpecPosition position = spec.position();
        check(position.current == 0 && position.next_step == KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, "1 체크포인트가 없으면 다음 단계 Step 1");
    }
    {
        Spec spec(root / "running");
        spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
        for (int index = 0; index < 3; ++index) { spec.add(DESIGN); }
        const KiottySpecPosition position = spec.position();
        check(position.next_step == KIOTTY_SPEC_STEP_DESIGN_DISCUSSING && position.pending_writes == 3, "2 진행 중인 Step 4, 묶인 쓰기 3");
    }
    {
        Spec spec(root / "entry");
        spec.start(KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED);
        for (const char* step : {"Step 4", "Step 2"}) {
            const std::string id = spec.add(CHANGE_REQUEST);
            spec.setTag(id, CHANGE_REQUEST, "상태", "열림");
            spec.setTag(id, CHANGE_REQUEST, "진입 단계", step);
        }
        spec.complete();
        check(spec.position().next_step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, "3 진입 단계 4 와 2 이면 다음 단계 Step 2");
    }
    {
        Spec spec(root / "end");
        spec.start(KIOTTY_SPEC_STEP_END_TO_END_TEST_RUN);
        spec.complete();
        check(spec.position().next_step == 0, "4 Step 12 가 확정되고 열린 변경 요청이 없으면 다음 단계 없음");
    }
    {
        Spec spec(root / "runs");
        for (KiottySpecStep step : {KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING}) {
            spec.start(step);
        }
        const KiottySpecCursorResult cursor = KiottySpec_openStepRunCursor(spec.context);
        KiottySpecStepRunPage page {};
        KiottySpec_fetchStepRunPage(spec.context, cursor.value, &page);
        KiottySpec_closeCursor(spec.context, cursor.value);
        bool runs = page.length == 2;
        if (runs) {
            const KiottySpecStepRun& first = page.elements[0].step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING ? page.elements[0] : page.elements[1];
            const KiottySpecStepRun& second = page.elements[0].step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING ? page.elements[1] : page.elements[0];
            runs = first.count == 2 && second.step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING && second.count == 1;
        }
        check(runs, "5 지나온 단계는 Step 2 구간(2) 과 Step 3 구간(1)");
    }
}

void st023(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const KiottySpecCheckpointHandle second = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const KiottySpecCheckpointHandle third = spec.start(KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    const KiottySpecCursorResult cursor = KiottySpec_openCheckpointCursor(spec.context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    KiottySpec_closeCursor(spec.context, cursor.value);
    check(cursor.total == 2, "1 Step 2 목록은 둘");
    const KiottySpecCheckpointResult latest = KiottySpec_readCheckpoint(spec.context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    check(sequenceOf(latest.handle) == sequenceOf(third) && sequenceOf(latest.value.previous) == sequenceOf(second) && first != second, "2 가장 최근은 셋째, 이전은 둘째");
}

void st024(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.add(DESIGN, "DSN-001", "처음");
    spec.complete();
    const KiottySpecCheckpointHandle base = spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    KiottySpec_updateItem(spec.context, textOf("DSN-001"), textOf("기준"), UNCHANGED);
    check(KiottySpec_pruneCheckpoints(spec.context, KIOTTY_SPEC_ACTOR_USER, base).code == KIOTTY_SPEC_STATUS_PRECONDITION, "2 진행 중인 체크포인트는 기준이 될 수 없다");
    spec.complete();
    check(KiottySpec_pruneCheckpoints(spec.context, KIOTTY_SPEC_ACTOR_AI, base).code == KIOTTY_SPEC_STATUS_PERMISSION, "3 AI 는 정리할 수 없다");
    const KiottySpecSizeResult pruned = KiottySpec_pruneCheckpoints(spec.context, KIOTTY_SPEC_ACTOR_USER, base);
    check(ok(pruned.code) && pruned.value == 1 && std::string(spec.item("DSN-001").title) == "기준", "1 이전 하나를 지우고 기준 시점 내용은 그대로");
}

// ---------------------------------------------------------------- REQ-025 ~ 029

KiottySpecDiffQuery range(KiottySpecCheckpointHandle from, uint32_t kinds = 0, uint32_t fields = 0)
{
    return KiottySpecDiffQuery{from, KIOTTY_SPEC_CHECKPOINT_CURRENT, kinds, fields};
}

uint32_t kindBit(KiottySpecItemKind kind) { return 1u << (static_cast<uint32_t>(kind) - 1); }

void st025(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.add(REQUIREMENT, "REQ-001", "처음");
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.add(REQUIREMENT, "REQ-002");
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("고침"), UNCHANGED);
    const KiottySpecKindDiff diff = summarizeDiff(spec.context, range(first)).kinds[REQUIREMENT - 1];
    check(diff.added == 1 && diff.modified == 1, "1 요구사항 추가 1, 수정 1");
}

void st026(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.functional("REQ-001");
    KiottySpec_writeDecision(spec.context, DESIGN, textOf("- 처음"));
    const KiottySpecCheckpointHandle second = spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.add(DESIGN, "DSN-001");
    spec.add(DESIGN, "DSN-002");
    spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-001");
    KiottySpec_writeDecision(spec.context, DESIGN, textOf("- 고침"));
    const KiottySpecKindDiff diff = summarizeDiff(spec.context, range(first)).kinds[DESIGN - 1];
    check(diff.added == 2 && diff.relations_added == 1 && diff.decision_changed == KIOTTY_SPEC_TRUE, "1 설계 사항 추가 2, 관계 추가 1, 결정 기록 바뀜");
    check(summarizeDiff(spec.context, KiottySpecDiffQuery{second, first, 0, 0}).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 거꾸로 된 구간 거부");
    spec.complete();
    KiottySpec_pruneCheckpoints(spec.context, KIOTTY_SPEC_ACTOR_USER, second);
    check(summarizeDiff(spec.context, range(first)).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "3 정리로 지운 체크포인트는 없다는 상태");
}

void st027(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.functional("REQ-001");
    spec.add(DESIGN, "DSN-001");
    spec.add(DESIGN, "DSN-002");
    spec.link("DSN-001", DESIGN, "관련 요구사항", "REQ-001");
    spec.start(KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    spec.body("DSN-001", "본문");
    KiottySpec_updateItem(spec.context, textOf("DSN-002"), textOf("새 제목"), UNCHANGED);
    KiottySpec_removeRelation(spec.context, textOf("DSN-001"), spec.relation(DESIGN, "관련 요구사항"), textOf("REQ-001"));
    const KiottySpecCursorResult items = openItemDiffCursor(spec.context, range(first, kindBit(DESIGN), KIOTTY_SPEC_CHANGED_FIELD_BODY));
    KiottySpecItemDiffPage page {};
    KiottySpec_fetchItemDiffPage(spec.context, items.value, &page);
    KiottySpec_closeCursor(spec.context, items.value);
    check(page.length == 1 && std::string(page.elements[0].id) == "DSN-001", "1 본문이 바뀐 설계 사항만");
    const KiottySpecCursorResult relations = openRelationDiffCursor(spec.context, range(first));
    KiottySpecRelationDiffPage changes {};
    KiottySpec_fetchRelationDiffPage(spec.context, relations.value, &changes);
    KiottySpec_closeCursor(spec.context, relations.value);
    check(changes.length == 1 && changes.elements[0].change == KIOTTY_SPEC_CHANGE_REMOVED, "2 끊은 관계가 삭제로 보인다");
}

void st028(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.add(REQUIREMENT, "REQ-001", "처음");
    spec.add(REQUIREMENT, "REQ-002", "지울 것");
    spec.body("REQ-001", "처음 본문");
    KiottySpec_writeDecision(spec.context, REQUIREMENT, textOf("- 처음 결정"));
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), textOf("고침"), UNCHANGED);
    KiottySpec_removeItem(spec.context, textOf("REQ-002"));
    spec.body("REQ-001", "고친 본문");
    KiottySpec_writeDecision(spec.context, REQUIREMENT, textOf("- 고친 결정"));
    check(std::string(KiottySpec_readItemAt(spec.context, textOf("REQ-001"), first).value.title) == "처음", "1 고치기 전 제목");
    check(ok(KiottySpec_readItemAt(spec.context, textOf("REQ-002"), first).code), "2 지운 항목도 그 시점에는 읽힌다");
    char buffer[256] {};
    check(KiottySpec_readItemBodyAt(spec.context, textOf("REQ-001"), first, KiottySpecBuffer{buffer, sizeof(buffer)}).code == KIOTTY_SPEC_STATUS_NOT_FOUND &&
              spec.failedWith(KIOTTY_SPEC_SUBJECT_PAST_BODY, "REQ-001"),
          "3 바뀐 본문은 없다는 상태와 git 이력 안내");
    KiottySpec_readDecisionAt(spec.context, REQUIREMENT, first, KiottySpecBuffer{buffer, sizeof(buffer)});
    check(std::string(buffer) == "- 처음 결정", "4 바꾸기 전 결정 기록");
}

void st029(const fs::path& root)
{
    Spec spec(root);
    const KiottySpecCheckpointHandle first = spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    spec.add(REQUIREMENT, "REQ-001", "제목", "저장한다");
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    KiottySpec_updateItem(spec.context, textOf("REQ-001"), UNCHANGED, textOf("모두 저장한다"));
    const KiottySpecDiffQuery points = range(first);
    const KiottySpecCursorResult cursor = KiottySpec_openTextDiffCursor(spec.context, spec.handle("REQ-001"), KIOTTY_SPEC_TEXT_FIELD_SUMMARY, &points);
    std::string added;
    std::string same;
    KiottySpecTextDiffPage page {};
    for (size_t fetched = 0; ok(cursor.code) && fetched < cursor.total; fetched += page.length) {
        if (!ok(KiottySpec_fetchTextDiffPage(spec.context, cursor.value, &page).code) || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            const std::string text(page.elements[index].text, page.elements[index].length);
            if (page.elements[index].change == KIOTTY_SPEC_TEXT_CHANGE_ADDED) { added += text; }
            if (page.elements[index].change == KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED) { same += text; }
        }
    }
    KiottySpec_closeCursor(spec.context, cursor.value);
    check(ok(cursor.code) && added == "모두 " && same == "저장한다", "1 더한 조각 \"모두 \" 와 같은 조각 \"저장한다\"");
    const KiottySpecCursorResult body =
        KiottySpec_openTextDiffCursor(spec.context, spec.handle("REQ-001"), static_cast<KiottySpecTextField>(KIOTTY_SPEC_CHANGED_FIELD_BODY), &points);
    check(body.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "2 본문의 글자 차이는 거부");
}

// ---------------------------------------------------------------- REQ-030 ~ 031

void st030(const fs::path& root)
{
    Spec spec(root);
    spec.start(KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    KiottySpec_writeDecision(spec.context, REQUIREMENT, textOf("- REQ-001 · 결정"));
    char buffer[64] {};
    KiottySpec_readDecision(spec.context, REQUIREMENT, KiottySpecBuffer{buffer, sizeof(buffer)});
    check(std::string(buffer) == "- REQ-001 · 결정", "1 다시 쓴 내용이 읽힌다");
    check(KiottySpec_writeDecision(spec.context, REQUIREMENT, textOf(std::string(KIOTTY_SPEC_MAX_DECISION_LENGTH + 1, 'a'))).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
          "2 2048 바이트를 넘으면 거부");
    check(KiottySpec_writeDecision(spec.context, CHANGE_REQUEST, textOf("- x")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "3 결정 기록이 없는 종류 거부");
}

void st031(const fs::path& root)
{
    {
        Spec spec(root / "no_entry");
        spec.start(KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED);
        spec.add(CHANGE_REQUEST, "CR-001");
        spec.setTag("CR-001", CHANGE_REQUEST, "상태", "열림");
        check(completeFailure(spec).find(unmet(KIOTTY_SPEC_CONDITION_ENTRY_STEP, "CR-001")) != std::string::npos,
              "1 진입 단계가 없는 열린 변경 요청이 있으면 그 ID 와 함께 거부");
    }
    {
        Spec spec(root / "entry");
        spec.start(KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED);
        spec.add(CHANGE_REQUEST, "CR-001");
        spec.setTag("CR-001", CHANGE_REQUEST, "상태", "열림");
        spec.setTag("CR-001", CHANGE_REQUEST, "진입 단계", "Step 4");
        spec.complete();
        check(spec.position().next_step == KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, "2 진입 단계 4 이면 다음 단계 Step 4");
    }
    {
        Spec spec(root / "closed");
        spec.start(KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED);
        spec.add(CHANGE_REQUEST, "CR-001");
        spec.setTag("CR-001", CHANGE_REQUEST, "상태", "완료");
        check(spec.position().open_change_requests == 0, "3 완료인 변경 요청만 있으면 열린 수 0");
    }
}

struct SystemTest {
    const char* id;
    const char* title;
    void (*run)(const fs::path& root);
};

const SystemTest TESTS[] = {
    {"ST-001", "프로젝트의 사양 기록을 파일 하나로 열고 보관한다", st001},
    {"ST-002", "사양 파일을 열고 처음이면 만든다", st002},
    {"ST-003", "프로젝트 이름과 만든 시각을 남긴다", st003},
    {"ST-004", "기록을 git 으로 보관하고 병합한다", st004},
    {"ST-005", "밖에서 바뀐 파일을 덮어쓰지 않는다", st005},
    {"ST-006", "형식이 다른 파일은 열지 않는다", st006},
    {"ST-007", "단계 산출물을 항목으로 기록한다", st007},
    {"ST-008", "항목을 만들고 ID 를 받는다", st008},
    {"ST-009", "항목의 제목 · 요약 · 본문을 고친다", st009},
    {"ST-010", "항목을 상위 · 하위로 묶는다", st010},
    {"ST-011", "항목을 지운다", st011},
    {"ST-012", "항목 ID 를 바꾼다", st012},
    {"ST-013", "항목을 찾고 목록으로 본다", st013},
    {"ST-014", "항목을 종류별 모델대로 분류하고 연결한다", st014},
    {"ST-015", "항목에 태그를 붙여 분류한다", st015},
    {"ST-016", "정해진 관계로만 연결한다", st016},
    {"ST-017", "모델을 읽는다", st017},
    {"ST-018", "변경의 영향을 따라간다", st018},
    {"ST-019", "단계를 따라 체크포인트로 기록을 쌓고 확정한다", st019},
    {"ST-020", "체크포인트를 시작하고 쓰기를 묶는다", st020},
    {"ST-021", "사용자만 체크포인트를 확정하고, 단계별 조건을 검사한다", st021},
    {"ST-022", "지금 어디에 있고 다음 단계가 무엇인지 안다", st022},
    {"ST-023", "체크포인트 목록과 이력을 본다", st023},
    {"ST-024", "오래된 기록을 정리한다", st024},
    {"ST-025", "체크포인트 사이에 바뀐 것을 본다", st025},
    {"ST-026", "바뀐 것을 종류별로 요약한다", st026},
    {"ST-027", "바뀐 항목과 관계를 목록으로 본다", st027},
    {"ST-028", "지난 시점의 내용을 읽는다", st028},
    {"ST-029", "제목과 요약의 글자 차이를 본다", st029},
    {"ST-030", "왜 그렇게 정했는지 남긴다 (결정 기록)", st030},
    {"ST-031", "변경 요청으로 흐름을 다시 시작한다", st031},
};

}

int main(int argc, char** argv)
{
    std::error_code error;
    g_base = fs::temp_directory_path(error) / "kiotty_system_test";
    fs::remove_all(g_base, error);
    int failed = 0;
    for (const SystemTest& test : TESTS) {
        if (argc > 1 && std::strcmp(argv[1], test.id) != 0) { continue; }
        g_passed = true;
        test.run(freshRoot(test.id));
        std::printf("%s %s %s\n", test.id, g_passed ? "PASS" : "FAIL", test.title);
        std::fflush(stdout);
        if (!g_passed) { ++failed; }
    }
    fs::remove_all(g_base, error);
    std::printf("%d failed\n", failed);
    return failed;
}
