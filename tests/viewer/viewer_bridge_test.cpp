// 뷰어 다리 단위 테스트 (UT-102 ~ UT-110). 라이브러리 C API 로 임시 사양 파일을 만들고 다리의 JSON 글을 본다.
// 네이티브와 wasm32 (Node.js) 두 빌드에서 같은 테스트가 돈다.

#include "kiotty_viewer_bridge.h"
#include "kiotty_viewer_json.h"

#include "kiotty_spec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace {

std::string g_root;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool contains(const std::string& text, const char* piece) { return text.find(piece) != std::string::npos; }

bool expectContains(const std::string& text, const char* piece, const char* label)
{
    const bool found = contains(text, piece);
    if (!found) { std::printf("  FAIL: %s\n    missing %s in %s\n", label, piece, text.c_str()); }
    return found;
}

KiottySpecText textOf(const char* text) { return KiottySpecText { text, std::strlen(text) }; }

bool ok(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

std::string pathOf(const char* name) { return g_root + "/" + name; }

KiottySpecIdResult createItem(KiottySpecContext* context, KiottySpecItemKind kind, const char* id, const char* title,
                              const char* summary)
{
    KiottySpecItemDraft draft {};
    draft.id = textOf(id);
    draft.title = textOf(title);
    draft.summary = textOf(summary);
    draft.kind = kind;
    return KiottySpec_createItem(context, &draft);
}

bool setTag(KiottySpecContext* context, const char* id, uint32_t tag, uint64_t value)
{
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf(id));
    return ok(handle.code) && ok(KiottySpec_setItemTag(context, handle.value, tag, value).code);
}

// 시험용 사양 파일 A: Step 2 진행 중. REQ-001 (태그 두 값, 본문, 하위 REQ-002), DSN-001 → REQ-001,
// CR-001 (상태 열림, 영향 요구사항 REQ-001 · 영향 설계 사항 DSN-001), CR-002 (상태 완료, 관계 없음)
bool buildFixtureA(const std::string& path)
{
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (!ok(created.code)) { return false; }
    KiottySpecContext* context = created.value;
    bool passed = ok(KiottySpec_open(context, textOf(path.c_str())).code);
    passed = ok(KiottySpec_initializeProject(context, textOf("뷰어 시험")).code) && passed;
    passed = ok(KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED, textOf("접수")).code) && passed;
    passed = ok(createItem(context, KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST, "", "뷰어 만들기", "브라우저에서 본다").code) && passed;
    passed = setTag(context, "CR-001", 0, 1) && passed;        // 출처 사용자 피드백
    passed = setTag(context, "CR-001", 1, 1) && passed;        // 상태 열림
    passed = setTag(context, "CR-001", 2, 1 << 1) && passed;   // 진입 단계 Step 2
    passed = ok(createItem(context, KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST, "", "끝난 요청", "").code) && passed;
    passed = setTag(context, "CR-002", 0, 1) && passed;
    passed = setTag(context, "CR-002", 1, 1 << 2) && passed;   // 상태 완료
    passed = setTag(context, "CR-002", 2, 1 << 1) && passed;
    passed = ok(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code) && passed;
    passed = ok(KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("요구사항")).code) && passed;
    passed = ok(createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "", "저장 \"안전\"", "한 줄\n둘째 줄").code) && passed;
    passed = setTag(context, "REQ-001", 0, (1 << 0) | (1 << 2)) && passed;   // 기능 · 환경
    passed = ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("## 진술\n<script>x</script>\n")).code) && passed;
    passed = ok(createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "", "하위", "").code) && passed;
    passed = ok(KiottySpec_setItemParent(context, textOf("REQ-002"), textOf("REQ-001")).code) && passed;
    passed = ok(createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "저장 모듈", "").code) && passed;
    passed = ok(KiottySpec_addRelation(context, textOf("DSN-001"), 0, textOf("REQ-001")).code) && passed;
    passed = ok(KiottySpec_addRelation(context, textOf("CR-001"), 0, textOf("REQ-001")).code) && passed;
    passed = ok(KiottySpec_addRelation(context, textOf("CR-001"), 3, textOf("DSN-001")).code) && passed;
    KiottySpec_releaseContext(context);
    return passed;
}

// 사양 파일 C: 체크포인트가 없다
bool buildFixtureC(const std::string& path)
{
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (!ok(created.code)) { return false; }
    const bool passed = ok(KiottySpec_open(created.value, textOf(path.c_str())).code);
    KiottySpec_releaseContext(created.value);
    return passed;
}

bool testJsonWriterEscapes()
{
    std::string out {};
    kiotty::viewer::JsonWriter json(out);
    json.open(kiotty::viewer::JsonScope::OBJECT);
    json.writeKey("q"); json.writeString("a\"b\\c\nd\x01" "한글");
    json.writeKey("e"); json.writeString("");
    json.writeKey("n"); json.open(kiotty::viewer::JsonScope::ARRAY); json.writeNumber(uint64_t { 0 }); json.writeNumber(uint64_t { 18446744073709551615u }); json.close(kiotty::viewer::JsonScope::ARRAY);
    json.writeKey("o"); json.open(kiotty::viewer::JsonScope::OBJECT); json.writeKey("t"); json.writeLiteral("true"); json.writeKey("z"); json.writeLiteral("null"); json.close(kiotty::viewer::JsonScope::OBJECT);
    json.close(kiotty::viewer::JsonScope::OBJECT);
    bool passed = expectContains(out, "\"q\":\"a\\\"b\\\\c\\nd\\u0001한글\"", "quote, backslash, control and Korean");
    passed = expectContains(out, "\"e\":\"\"", "empty string") && passed;
    passed = expectContains(out, "\"n\":[0,18446744073709551615]", "largest number is written in full") && passed;
    passed = expectContains(out, "\"o\":{\"t\":true,\"z\":null}", "nested object") && passed;
    passed = expectTrue(out.front() == '{' && out.back() == '}' && !contains(out, ",}") && !contains(out, ",]"), "commas only between members") && passed;
    return passed;
}

bool testQueryBeforeOpen()
{
    const std::string response = KiottyViewer_query("progress");
    bool passed = expectContains(response, "\"ok\":false", "ok is false");
    passed = expectContains(response, "\"status\":\"NOT_OPEN\"", "status is NOT_OPEN") && passed;
    return passed;
}

bool testOpenKeepsPrevious()
{
    const std::string opened = KiottyViewer_open(pathOf("a.kisp").c_str());
    bool passed = expectContains(opened, "\"ok\":true", "opening A succeeds");
    passed = expectContains(opened, "\"name\":\"뷰어 시험\"", "opening A returns project name") && passed;
    {
        std::ofstream bad(pathOf("b.kisp"), std::ios::binary);
        bad << "hello\nworld\n";
    }
    const std::string failed = KiottyViewer_open(pathOf("b.kisp").c_str());
    passed = expectContains(failed, "\"ok\":false", "opening B fails") && passed;
    passed = expectContains(failed, "\"line\":", "failure has a line") && passed;
    passed = expectTrue(!contains(failed, "\"line\":0"), "failure line is set") && passed;
    const std::string overview = KiottyViewer_query("overview");
    passed = expectContains(overview, "\"kind\":\"requirement\",\"name\":\"요구사항\",\"prefix\":\"REQ\",\"created_in\":2,\"count\":2", "overview after B still counts the items of A") && passed;
    const std::string missing = KiottyViewer_open(pathOf("none.kisp").c_str());
    passed = expectContains(missing, "\"status\":\"NOT_FOUND\",\"subject\":\"FILE\"", "missing file is NOT_FOUND FILE") && passed;
    return passed;
}

bool testOverviewCounts()
{
    const std::string overview = KiottyViewer_query("overview");
    bool passed = expectContains(overview, "\"ok\":true,\"project\":{\"name\":\"뷰어 시험\",\"created_at\":", "project is first");
    passed = expectContains(overview, "\"kinds\":[{\"kind\":\"change_request\"", "kinds follow model order") && passed;
    passed = expectContains(overview, "\"prefix\":\"DSN\",\"created_in\":4,\"count\":1", "design count is 1") && passed;
    passed = expectContains(overview, "\"prefix\":\"MS\",\"created_in\":6,\"count\":0", "kind without items is listed with count 0") && passed;
    return passed;
}

bool testProgressPosition()
{
    const std::string progress = KiottyViewer_query("progress");
    bool passed = expectContains(progress, "\"position\":{\"checkpoint\":\"CP-0002\",\"step\":2,\"state\":\"running\"", "checkpoint, step and state equal readPosition");
    passed = expectContains(progress, "\"description\":\"요구사항\"", "description of the current checkpoint") && passed;
    passed = expectContains(progress, "\"unmet\":[{\"id\":\"REQ-001\",\"kind\":\"requirement\",\"title\":\"저장 \\\"안전\\\"\",\"condition\":\"ACCEPTANCE_CRITERIA\"}", "unmet rows equal the unmet condition cursor") && passed;
    passed = expectContains(progress, "\"checkpoints\":[{\"checkpoint\":\"CP-0002\",\"step\":2,\"state\":\"running\",\"description\":\"요구사항\",\"created_at\":", "checkpoints are newest first") && passed;
    passed = expectContains(progress, "\"previous\":\"CP-0001\"},{\"checkpoint\":\"CP-0001\",\"step\":1,\"state\":\"done\"", "second checkpoint is the first one") && passed;
    passed = expectContains(progress, "\"previous\":null}]", "first checkpoint has no previous") && passed;

    const std::string opened = KiottyViewer_open(pathOf("c.kisp").c_str());
    passed = expectContains(opened, "\"ok\":true", "file without checkpoints opens") && passed;
    const std::string empty = KiottyViewer_query("progress");
    passed = expectContains(empty, "\"position\":null,\"unmet\":[],\"checkpoints\":[]", "file without checkpoints gives position null") && passed;
    passed = expectContains(KiottyViewer_open(pathOf("a.kisp").c_str()), "\"ok\":true", "reopen A") && passed;
    return passed;
}

bool testItemsByKind()
{
    const std::string items = KiottyViewer_query("items:requirement");
    bool passed = expectContains(items, "\"ok\":true,\"kind\":\"requirement\",\"model\":{\"name\":\"요구사항\",\"prefix\":\"REQ\",\"created_in\":2,\"tags\":[{\"index\":0,\"name\":\"요구사항 종류\",\"cardinality\":\"many\",\"values\":[\"기능 요구사항\",\"품질 요구사항\",\"환경 요구사항\"]}]", "model tags list name, values and cardinality");
    passed = expectContains(items, "\"relations\":[{\"index\":0,\"name\":\"관련 요구사항\",\"target\":\"requirement\"}]", "model relations") && passed;
    passed = expectContains(items, "\"items\":[{\"id\":\"REQ-001\",\"kind\":\"requirement\",\"title\":\"저장 \\\"안전\\\"\",\"summary\":\"한 줄\\n둘째 줄\",\"checkpoint\":\"CP-0002\",\"parent\":null,\"tags\":{\"요구사항 종류\":[\"기능 요구사항\",\"환경 요구사항\"]}}", "first item with every tag value") && passed;
    passed = expectContains(items, "{\"id\":\"REQ-002\",\"kind\":\"requirement\",\"title\":\"하위\",\"summary\":\"\",\"checkpoint\":\"CP-0002\",\"parent\":\"REQ-001\",\"tags\":{}}]", "parent field is set on the child, items are in ID order") && passed;
    const std::string unknown = KiottyViewer_query("items:nope");
    passed = expectContains(unknown, "\"ok\":false,\"error\":{\"status\":\"NOT_FOUND\",\"subject\":\"KIND\"", "unknown kind gives NOT_FOUND") && passed;
    return passed;
}

bool testItemDetail()
{
    const std::string requirement = KiottyViewer_query("item:REQ-001");
    bool passed = expectContains(requirement, "\"item\":{\"id\":\"REQ-001\",\"kind\":\"requirement\",\"title\":\"저장 \\\"안전\\\"\",\"summary\":\"한 줄\\n둘째 줄\",\"checkpoint\":\"CP-0002\",\"parent\":null,\"parent_title\":\"\"}", "item head");
    passed = expectContains(requirement, "\"body\":\"## 진술\\n<script>x</script>\\n\"", "body is returned as written") && passed;
    passed = expectContains(requirement, "\"out\":[]", "requirement has no out relations") && passed;
    passed = expectContains(requirement, "\"in\":[{\"relation\":\"영향 요구사항\",\"id\":\"CR-001\",\"kind\":\"change_request\",\"title\":\"뷰어 만들기\"},{\"relation\":\"관련 요구사항\",\"id\":\"DSN-001\",\"kind\":\"design\",\"title\":\"저장 모듈\"}]", "in relations list CR and DSN with relation name and title") && passed;
    passed = expectContains(requirement, "\"impact\":[{\"id\":\"CR-001\"", "impact lists CR-001") && passed;
    passed = expectContains(requirement, "{\"id\":\"DSN-001\",\"kind\":\"design\",\"title\":\"저장 모듈\"}],\"children\":[{\"id\":\"REQ-002\",\"kind\":\"requirement\",\"title\":\"하위\"}]", "impact lists DSN-001 and children equal the child cursor") && passed;

    const std::string design = KiottyViewer_query("item:DSN-001");
    passed = expectContains(design, "\"out\":[{\"relation\":\"관련 요구사항\",\"id\":\"REQ-001\",\"kind\":\"requirement\",\"title\":\"저장 \\\"안전\\\"\"}]", "design out relations list REQ-001") && passed;
    passed = expectContains(design, "\"body\":\"\"", "design has no body") && passed;

    const std::string child = KiottyViewer_query("item:REQ-002");
    passed = expectContains(child, "\"parent\":\"REQ-001\",\"parent_title\":\"저장 \\\"안전\\\"\"", "child has parent id and title") && passed;

    const std::string unknown = KiottyViewer_query("item:NOPE-1");
    passed = expectContains(unknown, "\"ok\":false,\"error\":{\"status\":\"NOT_FOUND\"", "unknown ID gives NOT_FOUND") && passed;
    const std::string bad = KiottyViewer_query("nope");
    passed = expectContains(bad, "\"status\":\"INVALID_ARGUMENT\"", "unknown request is INVALID_ARGUMENT") && passed;
    return passed;
}

bool testChangesOutCount()
{
    const std::string changes = KiottyViewer_query("changes");
    bool passed = expectContains(changes, "\"ok\":true,\"kind\":\"change_request\"", "changes are change requests");
    passed = expectContains(changes, "\"id\":\"CR-001\",\"kind\":\"change_request\",\"title\":\"뷰어 만들기\",\"summary\":\"브라우저에서 본다\",\"checkpoint\":\"CP-0001\",\"parent\":null,\"tags\":{\"출처\":[\"사용자 피드백\"],\"상태\":[\"열림\"],\"진입 단계\":[\"Step 2\"]},\"out_count\":2", "CR-001 has its status tag and out_count 2") && passed;
    passed = expectContains(changes, "\"id\":\"CR-002\"", "CR-002 is listed") && passed;
    passed = expectContains(changes, "\"상태\":[\"완료\"],\"진입 단계\":[\"Step 2\"]},\"out_count\":0", "CR-002 out_count 0") && passed;
    return passed;
}

} // namespace

int main()
{
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "kiotty_viewer_bridge_test";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    g_root = root.generic_string();
    if (!buildFixtureA(pathOf("a.kisp")) || !buildFixtureC(pathOf("c.kisp"))) {
        std::printf("FAIL build fixtures\n");
        return 1;
    }

    const std::pair<const char*, bool (*)()> tests[] = {
        {"json writer escapes text", testJsonWriterEscapes},
        {"query before open", testQueryBeforeOpen},
        {"open keeps previous on bad file", testOpenKeepsPrevious},
        {"overview counts by kind", testOverviewCounts},
        {"progress matches library position", testProgressPosition},
        {"items by kind", testItemsByKind},
        {"item detail with relations", testItemDetail},
        {"changes with out count", testChangesOutCount},
    };
    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    KiottyViewer_close();
    std::filesystem::remove_all(root, error);
    std::printf("%d failed\n", failed);
    return failed;
}
