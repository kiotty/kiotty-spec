// 공개 C 인터페이스를 임시 프로젝트 디렉토리에서 처음부터 끝까지 부른다.

#include "kiotty_spec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

// <root>/.kiotty/spec.kisp 를 연다. code 는 여는 데 실패한 이유다.
KiottySpecContextResult openContext(const KiottySpecConfiguration& configuration, const std::string& root)
{
    KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (!ok(created.code)) { return created; }
    const std::string database = root + "/.kiotty/spec.kisp";
    created.code = KiottySpec_open(created.value, textOf(database.c_str())).code;
    return created;
}

bool testWritesNeedCheckpoint(KiottySpecContext* context)
{
    bool passed = expectTrue(KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("a"), textOf("")).code == KIOTTY_SPEC_STATUS_PRECONDITION,
                             "write without checkpoint");
    const KiottySpecCheckpointHandleResult started = KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("요구사항"));
    passed = expectTrue(ok(started.code) && started.value == 1, "first checkpoint is 1") && passed;
    const KiottySpecCheckpointResult current = KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(current.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_STARTED, "started state") && passed;
    return passed;
}

bool testItemsTagsRelations(KiottySpecContext* context)
{
    const KiottySpecIdResult requirement = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("저장"), textOf("항목을 저장한다"));
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

    const KiottySpecIdResult interface = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, textOf(""), textOf("열기"), textOf(""));
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

std::string readAll(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

// 다른 컨텍스트로 다시 열어도 같은 내용이고, 바꾸지 않고 다시 써도 파일이 바이트 그대로다.
bool testReopen(KiottySpecContext* context)
{
    const std::string path = g_root + "/.kiotty/spec.kisp";
    const std::string before = readAll(path);
    bool passed = expectTrue(before.rfind("#kisp 2 model 1\n", 0) == 0, "file starts with header");
    passed = expectTrue(before.find("\nbody\tO\t") != std::string::npos &&before.find("## 인수 조건\\n- 저장된다\\n") != std::string::npos, "body line in file") && passed;
    passed = expectTrue(!std::filesystem::exists(g_root + "/.kiotty/spec"), "no body directory") && passed;
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult other = openContext(configuration, g_root);
    passed = expectTrue(ok(other.code), "reopen") && passed;
    const KiottySpecItemResult item = KiottySpec_readItem(other.value, textOf("REQ-001"));
    passed = expectTrue(ok(item.code) && std::strcmp(item.value.title, "저장") == 0, "item after reopen") && passed;
    char body[64] {};
    const KiottySpecSizeResult read = KiottySpec_readItemBody(other.value, textOf("REQ-001"), body, sizeof(body));
    passed = expectTrue(ok(read.code) && std::strcmp(body, "## 인수 조건\n- 저장된다\n") == 0, "body after reopen") && passed;
    passed = expectTrue(ok(KiottySpec_startCheckpoint(other.value, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, textOf("다시 열기")).code), "start in other") && passed;
    passed = expectTrue(ok(KiottySpec_resumeCheckpoint(other.value).code), "resume in other") && passed;
    KiottySpec_releaseContext(other.value);
    const std::string running = readAll(path);
    passed = expectTrue(running != before, "checkpoint written") && passed;
    const KiottySpecContextResult third = openContext(configuration, g_root);
    passed = expectTrue(ok(KiottySpec_resumeCheckpoint(third.value).code), "resume running checkpoint") && passed;
    passed = expectTrue(readAll(path) == running, "rewrite without change keeps bytes") && passed;
    KiottySpec_releaseContext(third.value);
    passed = expectTrue(ok(KiottySpec_findItem(context, textOf("REQ-001")).code), "first context still works") && passed;
    return passed;
}

// 연 뒤에 파일이 밖에서 바뀌면 쓰기를 CONFLICT 로 거부하고 다시 읽는다.
bool testOutsideChange(KiottySpecContext* context)
{
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult other = openContext(configuration, g_root);
    bool passed = expectTrue(ok(KiottySpec_updateItem(other.value, textOf("REQ-001"), textOf("다른 곳에서"), KiottySpecText{nullptr, 0}).code), "write from other");
    KiottySpec_releaseContext(other.value);
    const KiottySpecStatusResult stale = KiottySpec_updateItem(context, textOf("REQ-001"), textOf("늦은 쓰기"), KiottySpecText{nullptr, 0});
    passed = expectTrue(stale.code == KIOTTY_SPEC_STATUS_CONFLICT, "stale write rejected") && passed;
    const KiottySpecItemResult item = KiottySpec_readItem(context, textOf("REQ-001"));
    passed = expectTrue(ok(item.code) && std::strcmp(item.value.title, "다른 곳에서") == 0, "reloaded after conflict") && passed;
    passed = expectTrue(ok(KiottySpec_updateItem(context, textOf("REQ-001"), textOf("저장"), KiottySpecText{nullptr, 0}).code), "write after reload") && passed;
    return passed;
}

// ID 는 유일한 필드일 뿐이다. 관계 · 이력은 내부 키로 따라오므로 rename 뒤에도 그대로 이어진다.
bool testGivenIdAndRename(KiottySpecContext*)
{
    const std::string root = g_root + "/rename";
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = openContext(configuration, root);
    KiottySpecContext* context = created.value;
    bool passed = expectTrue(ok(created.code), "open rename project");
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, textOf("이름"));
    const KiottySpecIdResult given = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("DSN-010"), textOf("주어진 ID"), textOf(""));
    passed = expectTrue(ok(given.code) && std::strcmp(given.value, "DSN-010") == 0, "given id") && passed;
    const KiottySpecIdResult issued = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("발급"), textOf(""));
    passed = expectTrue(ok(issued.code) && std::strcmp(issued.value, "DSN-011") == 0, "issued id skips past given id") && passed;
    passed = expectTrue(KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("DSN-010"), textOf("겹침"), textOf("")).code ==
                            KIOTTY_SPEC_STATUS_CONFLICT,
                        "duplicate id rejected") &&
             passed;
    passed = expectTrue(KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("_bad"), textOf("잘못"), textOf("")).code ==
                            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "invalid id rejected") &&
             passed;
    const KiottySpecIdResult requirement = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구"), textOf(""));
    passed = expectTrue(ok(KiottySpec_addRelation(context, textOf("DSN-010"), 0, textOf(requirement.value)).code), "relate") && passed;
    KiottySpec_writeItemBody(context, textOf("DSN-010"), textOf("본문"));
    KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER);

    passed = expectTrue(KiottySpec_renameItem(context, textOf("DSN-010"), textOf("DSN-011")).code == KIOTTY_SPEC_STATUS_CONFLICT,
                        "rename onto existing id rejected") &&
             passed;
    passed = expectTrue(ok(KiottySpec_renameItem(context, textOf("DSN-010"), textOf("LOGIN")).code), "rename") && passed;
    passed = expectTrue(KiottySpec_readItem(context, textOf("DSN-010")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "old id gone") && passed;
    passed = expectTrue(ok(KiottySpec_readItem(context, textOf("LOGIN")).code), "new id found") && passed;
    char body[16] {};
    passed = expectTrue(ok(KiottySpec_readItemBody(context, textOf("LOGIN"), body, sizeof(body)).code) && std::strcmp(body, "본문") == 0,
                        "body follows rename") &&
             passed;
    const KiottySpecCursorResult relations = KiottySpec_openRelationCursor(context, textOf("LOGIN"), KIOTTY_SPEC_DIRECTION_BOTH);
    passed = expectTrue(ok(relations.code) && relations.total == 1, "relation follows rename") && passed;
    KiottySpec_closeCursor(context, relations.value);
    const KiottySpecItemResult past = KiottySpec_readItemAt(context, textOf("LOGIN"), KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(ok(past.code), "history follows rename") && passed;

    // 다시 열어도 (풀을 DB 에서 다시 적재해도) 새 ID 다.
    KiottySpec_releaseContext(context);
    const KiottySpecContextResult reopened = openContext(configuration, root);
    passed = expectTrue(ok(KiottySpec_readItem(reopened.value, textOf("LOGIN")).code), "rename persists") && passed;
    KiottySpec_releaseContext(reopened.value);
    return passed;
}

// 설계서 v1 5.4 의 호출 예와 같은 모양의 변화를 두 체크포인트에 걸쳐 만든다.
bool testDiff(KiottySpecContext*)
{
    const std::string root = g_root + "/diff";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = openContext(configuration, root);
    KiottySpecContext* context = created.value;
    const KiottySpecText none {nullptr, 0};

    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, textOf("설계 1"));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구 1"), textOf(""));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구 2"), textOf(""));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("본문 저장"), textOf("본문은 ID로만 읽는다"));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("파일 추적"), textOf(""));
    KiottySpec_addRelation(context, textOf("DSN-002"), 0, textOf("REQ-001"));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("링크 그래프"), textOf("관계를 링크 타입으로 검사한다"));
    KiottySpec_addRelation(context, textOf("DSN-003"), 0, textOf("REQ-001"));
    KiottySpec_setItemTag(context, KiottySpec_findItem(context, textOf("DSN-003")).value, 1, 1);
    bool passed = expectTrue(ok(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code), "complete first");

    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, textOf("설계 2"));
    KiottySpec_removeItem(context, textOf("DSN-002"));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("커서 풀"), textOf(""));
    KiottySpec_addRelation(context, textOf("DSN-004"), 0, textOf("REQ-002"));
    KiottySpec_updateItem(context, textOf("DSN-003"), textOf("관계 저장소"), textOf("관계를 모델의 관계 정의로 검사한다"));
    KiottySpec_setItemTag(context, KiottySpec_findItem(context, textOf("DSN-003")).value, 1, 4);
    KiottySpec_removeRelation(context, textOf("DSN-003"), 0, textOf("REQ-001"));
    KiottySpec_addRelation(context, textOf("DSN-003"), 0, textOf("REQ-002"));
    KiottySpec_writeItemBody(context, textOf("DSN-001"), textOf("## 내용\n바뀜\n"));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("잠깐"), textOf(""));
    KiottySpec_removeItem(context, textOf("DSN-005"));

    KiottySpecDiffQuery query {1, KIOTTY_SPEC_CHECKPOINT_CURRENT, 1u << (KIOTTY_SPEC_ITEM_KIND_DESIGN - 1), 0};
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(context, query);
    const KiottySpecKindDiff& design = summary.kinds[KIOTTY_SPEC_ITEM_KIND_DESIGN - 1];
    passed = expectTrue(ok(summary.code) && summary.to == 2, "summary range") && passed;
    passed = expectTrue(design.added == 1 && design.modified == 2 && design.removed == 1, "summary items") && passed;
    passed = expectTrue(design.relations_added == 2 && design.relations_removed == 2, "summary relations") && passed;
    passed = expectTrue(summary.kinds[KIOTTY_SPEC_ITEM_KIND_REQUIREMENT - 1].added == 0, "kind mask") && passed;

    const KiottySpecCursorResult items = KiottySpec_openItemDiffCursor(context, query);
    KiottySpecItemDiffPage page {};
    KiottySpec_fetchItemDiffPage(context, items.value, &page);
    passed = expectTrue(page.length == 4, "four item diffs") && passed;
    if (page.length == 4) {
        passed = expectTrue(std::strcmp(page.elements[0].id, "DSN-001") == 0 && page.elements[0].changed_fields == KIOTTY_SPEC_CHANGED_FIELD_BODY, "body only") && passed;
        passed = expectTrue(page.elements[1].change == KIOTTY_SPEC_CHANGE_REMOVED && page.elements[1].item == KIOTTY_SPEC_INVALID_ITEM_HANDLE &&
                                page.elements[1].before == 1 && page.elements[1].after == 2, "removed item") && passed;
        passed = expectTrue(page.elements[2].changed_fields == (KIOTTY_SPEC_CHANGED_FIELD_TITLE | KIOTTY_SPEC_CHANGED_FIELD_SUMMARY | KIOTTY_SPEC_CHANGED_FIELD_TAGS),
                            "modified fields") && passed;
        passed = expectTrue(page.elements[3].change == KIOTTY_SPEC_CHANGE_ADDED && page.elements[3].before == 0, "added item") && passed;
    }
    passed = expectTrue(KiottySpec_fetchTagPage(context, items.value, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null page") && passed;
    KiottySpec_closeCursor(context, items.value);

    query.field_mask = KIOTTY_SPEC_CHANGED_FIELD_TITLE;
    passed = expectTrue(KiottySpec_openItemDiffCursor(context, query).total == 3, "field mask keeps added and removed") && passed;
    query.field_mask = 0;

    const KiottySpecCursorResult relations = KiottySpec_openRelationDiffCursor(context, query);
    KiottySpecRelationDiffPage relation_page {};
    KiottySpec_fetchRelationDiffPage(context, relations.value, &relation_page);
    passed = expectTrue(relation_page.length == 4 && std::strcmp(relation_page.elements[0].from_id, "DSN-002") == 0 &&
                            relation_page.elements[0].change == KIOTTY_SPEC_CHANGE_REMOVED && std::strcmp(relation_page.elements[2].to_id, "REQ-002") == 0 &&
                            relation_page.elements[2].change == KIOTTY_SPEC_CHANGE_ADDED && relation_page.elements[2].checkpoint == 2,
                        "relation diffs") && passed;
    KiottySpec_closeCursor(context, relations.value);

    passed = expectTrue(KiottySpec_readItemAt(context, textOf("DSN-002"), 2).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "removed at to") && passed;
    const KiottySpecItemResult old = KiottySpec_readItemAt(context, textOf("DSN-003"), 1);
    passed = expectTrue(ok(old.code) && std::strcmp(old.value.title, "링크 그래프") == 0 && old.value.checkpoint == 1, "read item at") && passed;
    passed = expectTrue(KiottySpec_readItemTagAt(context, textOf("DSN-003"), 1, 1).value.value == 1, "read tag at") && passed;
    char body[64] {};
    passed = expectTrue(KiottySpec_readItemBodyAt(context, textOf("DSN-001"), 1, body, sizeof(body)).value == 0, "old body empty") && passed;

    const KiottySpecItemHandle handle = KiottySpec_findItem(context, textOf("DSN-003")).value;
    const KiottySpecCursorResult text = KiottySpec_openTextDiffCursor(context, handle, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, 1, 2);
    passed = expectTrue(ok(text.code) && text.total == 4, "text diff pieces") && passed;
    const char* expected[] = {"관계를 ", "링크 타입으로", "모델의 관계 정의로", " 검사한다"};
    const KiottySpecTextChange changes[] = {KIOTTY_SPEC_TEXT_UNCHANGED, KIOTTY_SPEC_TEXT_REMOVED, KIOTTY_SPEC_TEXT_ADDED, KIOTTY_SPEC_TEXT_UNCHANGED};
    for (int index = 0; index < 4; ++index) {
        const KiottySpecTextDiffResult piece = KiottySpec_nextTextDiff(context, text.value);
        passed = expectTrue(piece.value.change == changes[index] && piece.value.length == std::strlen(expected[index]) &&
                                std::memcmp(piece.value.text, expected[index], piece.value.length) == 0, expected[index]) && passed;
    }
    passed = expectTrue(KiottySpec_nextTextDiff(context, text.value).value.length == 0, "end of pieces") && passed;
    KiottySpec_closeTextDiffCursor(context, text.value);
    passed = expectTrue(KiottySpec_nextTextDiff(context, text.value).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "closed cursor") && passed;

    passed = expectTrue(KiottySpec_openTextDiffCursor(context, handle, static_cast<KiottySpecTextField>(KIOTTY_SPEC_CHANGED_FIELD_BODY), 1, 2).code ==
                            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "body rejected") && passed;
    passed = expectTrue(KiottySpec_openTextDiffCursor(context, KIOTTY_SPEC_INVALID_ITEM_HANDLE, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, 1, 2).code ==
                            KIOTTY_SPEC_STATUS_INVALID_HANDLE, "invalid handle") && passed;
    const KiottySpecCursorResult wrong = KiottySpec_openItemDiffCursor(context, query);
    passed = expectTrue(KiottySpec_nextTextDiff(context, wrong.value).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "wrong cursor type") && passed;
    KiottySpec_closeCursor(context, wrong.value);
    query.from = 2;
    query.to = 1;
    passed = expectTrue(KiottySpec_summarizeDiff(context, query).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "reversed range") && passed;
    query.from = 9;
    query.to = KIOTTY_SPEC_CHECKPOINT_CURRENT;
    passed = expectTrue(KiottySpec_summarizeDiff(context, query).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing checkpoint") && passed;
    (void)none;
    KiottySpec_releaseContext(context);
    return passed;
}


// 쓰기가 중간에 실패하면 그 쓰기 안에서 바뀐 것(ID 카운터)이 모두 되돌려진다.
bool testFailedWriteRollsBack()
{
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "kiotty_api_rollback";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::string path = root.generic_string();
    KiottySpecConfiguration configuration {};
    configuration.max_items = 2;
    const KiottySpecContextResult created = openContext(configuration, path);
    KiottySpecContext* const context = created.value;
    bool passed = expectTrue(ok(created.code), "create small context");
    passed = expectTrue(ok(KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("작은 풀")).code), "start") && passed;
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("a"), textOf(""));
    const KiottySpecIdResult second = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("b"), textOf(""));
    const KiottySpecIdResult full = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("c"), textOf(""));
    passed = expectTrue(full.code == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "third item does not fit") && passed;
    passed = expectTrue(ok(KiottySpec_removeItem(context, textOf(second.value)).code), "remove second") && passed;
    const KiottySpecIdResult again = KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("d"), textOf(""));
    passed = expectTrue(ok(again.code) && std::strcmp(again.value, "REQ-003") == 0, "failed write gave its id back") && passed;
    KiottySpec_releaseContext(context);
    std::filesystem::remove_all(root, error);
    return passed;
}

// 본문 영역이 작으면 고친 본문을 붙이기 전에 앞으로 당기고, 그래도 모자라면 CAPACITY_EXCEEDED 이며 되돌린다.
bool testSmallBodyArea()
{
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "kiotty_api_bodies";
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const std::string path = root.generic_string();
    KiottySpecConfiguration configuration {};
    configuration.max_body_bytes = 16;
    const KiottySpecContextResult created = openContext(configuration, path);
    KiottySpecContext* const context = created.value;
    bool passed = expectTrue(ok(created.code), "create small body context");
    passed = expectTrue(ok(KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("작은 본문")).code), "start") && passed;
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("REQ-001"), textOf("a"), textOf(""));
    KiottySpec_createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("REQ-002"), textOf("b"), textOf(""));
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("aaaaaa")).code), "first body") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-002"), textOf("bbbbbb")).code), "second body") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("cccccccc")).code), "rewrite compacts") && passed;
    passed = expectTrue(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("ddddddddddd")).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "body area full") && passed;
    char body[32] {};
    KiottySpec_readItemBody(context, textOf("REQ-001"), body, sizeof(body));
    passed = expectTrue(std::strcmp(body, "cccccccc") == 0, "failed body write rolled back") && passed;
    KiottySpec_readItemBody(context, textOf("REQ-002"), body, sizeof(body));
    passed = expectTrue(std::strcmp(body, "bbbbbb") == 0, "other body kept") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-002"), textOf("")).code), "empty body") && passed;
    const KiottySpecSizeResult empty = KiottySpec_readItemBody(context, textOf("REQ-002"), body, sizeof(body));
    passed = expectTrue(ok(empty.code) && empty.value == 0 && body[0] == '\0', "empty body reads empty") && passed;
    KiottySpec_releaseContext(context);
    std::filesystem::remove_all(root, error);
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
    const KiottySpecContextResult created = openContext(configuration, g_root);
    if (!ok(created.code)) {
        std::printf("FAIL create context\n");
        KiottySpec_releaseContext(created.value);
        return 1;
    }

    const std::pair<const char*, Failure> tests[] = {
        {"writes need checkpoint", testWritesNeedCheckpoint},
        {"items tags relations", testItemsTagsRelations},
        {"complete", testComplete},
        {"reopen", testReopen},
        {"outside change", testOutsideChange},
        {"diff", testDiff},
        {"given id and rename", testGivenIdAndRename},
    };
    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second(created.value);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    KiottySpec_releaseContext(created.value);
    std::filesystem::remove_all(root, error);
    const bool rolled_back = testFailedWriteRollsBack();
    std::printf("%s failed write rolls back\n", rolled_back ? "PASS" : "FAIL");
    if (!rolled_back) { ++failed; }
    const bool small_bodies = testSmallBodyArea();
    std::printf("%s small body area\n", small_bodies ? "PASS" : "FAIL");
    if (!small_bodies) { ++failed; }
    std::printf("%d failed\n", failed);
    return failed;
}
