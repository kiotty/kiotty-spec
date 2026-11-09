// 공개 C 인터페이스를 임시 프로젝트 디렉토리에서 처음부터 끝까지 부른다.

#include "kiotty_spec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>

namespace {

using Failure = std::function<bool(KiottySpecContext*)>;

std::string g_root;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

KiottySpecText textOf(const char* text) { return KiottySpecText{text, std::strlen(text)}; }

bool ok(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

bool testWritesNeedCheckpoint(KiottySpecContext* context)
{
    bool passed = expectTrue(KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("a"), textOf("")).code == KIOTTY_SPEC_STATUS_PRECONDITION,
                             "write without checkpoint");
    const KiottySpecCheckpointHandleResult started = KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("요구사항"));
    passed = expectTrue(ok(started.code) && started.value == 1, "first checkpoint is 1") && passed;
    const KiottySpecCheckpointResult current = KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(current.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_STARTED, "started state") && passed;
    return passed;
}

bool testItemsTagsRelations(KiottySpecContext* context)
{
    const KiottySpecIdResult requirement = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("저장"), textOf("항목을 저장한다"));
    bool passed = expectTrue(ok(requirement.code) && std::strcmp(requirement.value, "REQ-001") == 0, "requirement id");
    passed = expectTrue(KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
                        "first write runs checkpoint") && passed;
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf("REQ-001"));
    passed = expectTrue(ok(KiottySpec_setItemTag(context, handle.value, 0, 1).code), "set functional tag") && passed;
    passed = expectTrue(KiottySpec_setItemTag(context, handle.value, 0, 1u << 5).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "unnamed bit") && passed;
    passed = expectTrue(KiottySpec_readItemTag(context, handle.value, 0).value.value == 1, "read tag") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("## 인수 조건\n- 저장된다\n")).code), "write body") && passed;
    char body[64] {};
    const KiottySpecSizeResult read = KiottySpec_readItemBody(context, textOf("REQ-001"), body, sizeof(body));
    passed = expectTrue(ok(read.code) && std::strstr(body, "저장된다") != nullptr, "read body") && passed;
    passed = expectTrue(KiottySpec_readItemBody(context, textOf("REQ-001"), body, 4).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "small buffer") && passed;

    const KiottySpecIdResult interface = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, textOf("열기"), textOf(""));
    passed = expectTrue(ok(KiottySpec_addRelation(context, textOf(interface.value), 0, textOf("REQ-001")).code), "add relation") && passed;
    passed = expectTrue(KiottySpec_addRelation(context, textOf(interface.value), 0, textOf("REQ-001")).code == KIOTTY_SPEC_STATUS_CONFLICT,
                        "duplicate relation") && passed;
    passed = expectTrue(KiottySpec_addRelation(context, textOf("REQ-001"), 0, textOf(interface.value)).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "relation target kind") && passed;

    KiottySpecTagQuery query {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, 0, 1};
    const KiottySpecCursorResult cursor = KiottySpec_openItemCursor(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, &query, 1);
    passed = expectTrue(ok(cursor.code) && cursor.total == 1, "tag query") && passed;
    KiottySpecItemPage page {};
    KiottySpec_fetchItemPage(context, cursor.value, &page);
    passed = expectTrue(page.length == 1 && std::strcmp(page.elements[0].summary, "항목을 저장한다") == 0, "item page") && passed;
    KiottySpec_closeCursor(context, cursor.value);

    const KiottySpecCursorResult impact = KiottySpec_openImpactCursor(context, textOf("REQ-001"), 1);
    KiottySpecIdPage ids {};
    KiottySpec_fetchIdPage(context, impact.value, &ids);
    passed = expectTrue(ids.length == 1 && std::strcmp(ids.elements[0], interface.value) == 0, "impact ids") && passed;
    KiottySpec_closeCursor(context, impact.value);
    return passed;
}

bool testComplete(KiottySpecContext* context)
{
    bool passed = expectTrue(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_AI).code == KIOTTY_SPEC_STATUS_PERMISSION, "ai cannot complete");
    passed = expectTrue(ok(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code), "user completes with criteria") && passed;
    passed = expectTrue(KiottySpec_updateItem(context, textOf("REQ-001"), textOf("x"), KiottySpecText{nullptr, 0}).code == KIOTTY_SPEC_STATUS_CONFIRMED,
                        "done checkpoint rejects writes") && passed;
    const KiottySpecPositionResult position = KiottySpec_readPosition(context);
    passed = expectTrue(position.value.next_step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING, "next step") && passed;
    return passed;
}

bool testRoundTrip(KiottySpecContext* context)
{
    const std::string first = g_root + "/out1";
    const std::string second = g_root + "/out2";
    bool passed = expectTrue(ok(KiottySpec_exportJsonl(context, textOf(first.c_str())).code), "export");
    char report[256] {};
    passed = expectTrue(ok(KiottySpec_importJsonl(context, textOf(first.c_str()), report, sizeof(report)).code), "import") && passed;
    passed = expectTrue(ok(KiottySpec_exportJsonl(context, textOf(second.c_str())).code), "export again") && passed;
    for (const char* name : {"item_version.jsonl", "relation_version.jsonl", "checkpoint.jsonl"}) {
        std::error_code error;
        const auto left = std::filesystem::file_size(first + "/" + name, error);
        const auto right = std::filesystem::file_size(second + "/" + name, error);
        passed = expectTrue(!error && left == right && left > 0, name) && passed;
    }
    passed = expectTrue(ok(KiottySpec_findItem(context, textOf("REQ-001")).code), "item after import") && passed;
    return passed;
}

}

int main()
{
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "kiotty_api_test";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    g_root = root.generic_string();

    KiottySpecConfiguration configuration {};
    configuration.project_root = textOf(g_root.c_str());
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (!ok(created.code)) {
        std::printf("FAIL create context\n");
        KiottySpec_releaseContext(created.value);
        return 1;
    }

    const std::pair<const char*, Failure> tests[] = {
        {"writes need checkpoint", testWritesNeedCheckpoint},
        {"items tags relations", testItemsTagsRelations},
        {"complete", testComplete},
        {"round trip", testRoundTrip},
    };
    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second(created.value);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    KiottySpec_releaseContext(created.value);
    std::filesystem::remove_all(root, error);
    std::printf("%d failed\n", failed);
    return failed;
}
