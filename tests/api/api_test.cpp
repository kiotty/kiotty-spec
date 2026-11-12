// 공개 C 인터페이스를 임시 프로젝트 디렉토리에서 처음부터 끝까지 부른다.

#include "kiotty_spec.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace {

using Failure = std::function<bool(KiottySpecContext*)>;

std::string g_root;
KiottySpecError g_last_error {};

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

KiottySpecText textOf(const char* text) { return KiottySpecText{text, std::strlen(text)}; }

bool ok(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

KiottySpecIdResult createItem(KiottySpecContext* context, KiottySpecItemKind kind, KiottySpecText id, KiottySpecText title, KiottySpecText summary)
{
    KiottySpecItemDraft draft {};
    draft.id = id;
    draft.title = title;
    draft.summary = summary;
    draft.kind = kind;
    return KiottySpec_createItem(context, &draft);
}

KiottySpecTagQuery tagQuery(KiottySpecItemKind kind, uint32_t tag_index, uint64_t mask)
{
    KiottySpecTagQuery query {};
    query.mask = mask;
    query.kind = kind;
    query.tag_index = tag_index;
    return query;
}

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
    bool passed = expectTrue(createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("a"), textOf("")).code == KIOTTY_SPEC_STATUS_PRECONDITION,
                             "write without checkpoint");
    const KiottySpecCheckpointHandleResult started = KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("요구사항"));
    passed = expectTrue(ok(started.code) && started.value == 1, "first checkpoint is 1") && passed;
    const KiottySpecCheckpointResult current = KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed = expectTrue(current.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_STARTED, "started state") && passed;
    return passed;
}

bool testItemsTagsRelations(KiottySpecContext* context)
{
    const KiottySpecIdResult requirement = createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("저장"), textOf("항목을 저장한다"));
    bool passed = expectTrue(ok(requirement.code) && std::strcmp(requirement.value, "REQ-001") == 0, "requirement id");
    passed = expectTrue(KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
                        "first write runs checkpoint") && passed;
    const KiottySpecItemHandleResult handle = KiottySpec_findItem(context, textOf("REQ-001"));
    passed = expectTrue(ok(KiottySpec_setItemTag(context, handle.value, 0, 1).code), "set functional tag") && passed;
    passed = expectTrue(KiottySpec_setItemTag(context, handle.value, 0, 1u << 5).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "unnamed bit") && passed;
    passed = expectTrue(KiottySpec_readItemTag(context, handle.value, 0).value.value == 1, "read tag") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("## 인수 조건\n- 저장된다\n")).code), "write body") && passed;
    char body[64] {};
    const KiottySpecSizeResult read = KiottySpec_readItemBody(context, textOf("REQ-001"), KiottySpecBuffer{body, sizeof(body)});
    passed = expectTrue(ok(read.code) && std::strstr(body, "저장된다") != nullptr, "read body") && passed;
    passed = expectTrue(KiottySpec_readItemBody(context, textOf("REQ-001"), KiottySpecBuffer{body, 4}).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "small buffer") && passed;

    const KiottySpecIdResult interface = createItem(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, textOf(""), textOf("열기"), textOf(""));
    passed = expectTrue(ok(KiottySpec_addRelation(context, textOf(interface.value), 0, textOf("REQ-001")).code), "add relation") && passed;
    passed = expectTrue(KiottySpec_addRelation(context, textOf(interface.value), 0, textOf("REQ-001")).code == KIOTTY_SPEC_STATUS_CONFLICT,
                        "duplicate relation") && passed;
    passed = expectTrue(KiottySpec_addRelation(context, textOf("REQ-001"), 0, textOf(interface.value)).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "relation target kind") && passed;

    const KiottySpecTagQuery query = tagQuery(KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, 0, 1);
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

// enum · 정수 인자와 ID 형식은 C 인터페이스에서 거절되고, 아무것도 쓰지 않는다 (2.1).
bool testValueBoundaries(KiottySpecContext* context)
{
    const KiottySpecStatus invalid = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    const KiottySpecItemKind no_kind = static_cast<KiottySpecItemKind>(0);
    const KiottySpecItemKind past_kind = static_cast<KiottySpecItemKind>(14);
    const KiottySpecCheckpointHandle high_bits = (KiottySpecCheckpointHandle{1} << 32) | 1;
    bool passed = expectTrue(createItem(context, no_kind, textOf(""), textOf("a"), textOf("")).code == invalid, "kind 0");
    passed = expectTrue(createItem(context, past_kind, textOf(""), textOf("a"), textOf("")).code == invalid, "kind past the last") && passed;
    passed = expectTrue(createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("A B"), textOf("a"), textOf("")).code == invalid,
                        "malformed given id") && passed;
    passed = expectTrue(KiottySpec_findItem(context, textOf("-A")).code == invalid, "malformed id lookup") && passed;
    passed = expectTrue(KiottySpec_renameItem(context, textOf("REQ-001"), textOf("")).code == invalid, "empty new id") && passed;
    const KiottySpecItemHandle handle = KiottySpec_findItem(context, textOf("REQ-001")).value;
    passed = expectTrue(KiottySpec_setItemTag(context, handle, 99, 1).code == invalid, "tag past the model") && passed;
    passed = expectTrue(!ok(KiottySpec_setItemTag(context, KIOTTY_SPEC_INVALID_ITEM_HANDLE, 0, 1).code), "invalid item handle") && passed;
    passed = expectTrue(KiottySpec_writeDecision(context, no_kind, textOf("- x")).code == invalid, "decision kind 0") && passed;
    passed = expectTrue(KiottySpec_readDecision(context, past_kind, KiottySpecBuffer{nullptr, 0}).code == invalid, "read decision kind") && passed;
    passed = expectTrue(KiottySpec_startCheckpoint(context, static_cast<KiottySpecStep>(0), textOf("")).code == invalid, "step 0") && passed;
    passed = expectTrue(KiottySpec_startCheckpoint(context, static_cast<KiottySpecStep>(13), textOf("")).code == invalid, "step past the last") && passed;
    passed = expectTrue(KiottySpec_completeCheckpoint(context, static_cast<KiottySpecActor>(0)).code == invalid, "actor 0") && passed;
    passed = expectTrue(KiottySpec_completeCheckpoint(context, static_cast<KiottySpecActor>(3)).code == invalid, "actor past the last") && passed;
    passed = expectTrue(KiottySpec_pruneCheckpoints(context, static_cast<KiottySpecActor>(3), 1).code == invalid, "prune actor") && passed;
    passed = expectTrue(KiottySpec_pruneCheckpoints(context, KIOTTY_SPEC_ACTOR_USER, high_bits).code == invalid, "prune high bits") && passed;
    passed = expectTrue(KiottySpec_readCheckpoint(context, high_bits).code == invalid, "read checkpoint high bits") && passed;
    passed = expectTrue(KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
                        "checkpoint stays running") && passed;
    return passed;
}

// 질의 · 비교 함수의 enum · 정수 · 체크포인트 인자도 C 인터페이스에서 거절된다 (2.1, CR-014).
bool testQueryBoundaries(KiottySpecContext* context)
{
    const KiottySpecStatus invalid = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    const KiottySpecCheckpointHandle high_bits = (KiottySpecCheckpointHandle{1} << 32) | 1;
    const KiottySpecItemHandle item = KiottySpec_findItem(context, textOf("REQ-001")).value;
    KiottySpecDiffQuery high_range {};
    high_range.from = high_bits;
    bool passed = expectTrue(KiottySpec_summarizeDiff(context, &high_range).code == invalid, "diff from high bits");
    passed = expectTrue(KiottySpec_openItemDiffCursor(context, &high_range).code == invalid, "item diff high bits") && passed;
    passed = expectTrue(KiottySpec_openRelationDiffCursor(context, &high_range).code == invalid, "relation diff high bits") && passed;
    passed = expectTrue(KiottySpec_readItemAt(context, textOf("REQ-001"), high_bits).code == invalid, "read at high bits") && passed;
    passed = expectTrue(KiottySpec_readDecisionAt(context, KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST, 1, KiottySpecBuffer{nullptr, 0}).code == invalid,
                        "decision at kind without decision") && passed;
    KiottySpecTagQuery tag_query {};
    tag_query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
    tag_query.tag_index = 99;
    passed = expectTrue(KiottySpec_openItemCursor(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, &tag_query, 1).code == invalid,
                        "tag query past the model") && passed;
    passed = expectTrue(KiottySpec_openItemCursor(context, static_cast<KiottySpecItemKind>(14), nullptr, 0).code == invalid,
                        "item cursor kind past the last") && passed;
    passed = expectTrue(KiottySpec_openRelationCursor(context, textOf("REQ-001"), static_cast<KiottySpecDirection>(9)).code == invalid,
                        "direction past the last") && passed;
    passed = expectTrue(KiottySpec_openRelationCursorByModel(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, 99).code == invalid,
                        "relation index past the model") && passed;
    passed = expectTrue(KiottySpec_addRelation(context, textOf("REQ-001"), 99, textOf("REQ-001")).code == invalid,
                        "add relation past the model") && passed;
    passed = expectTrue(KiottySpec_openCheckpointCursor(context, static_cast<KiottySpecStep>(13)).code == invalid, "step filter past the last") && passed;
    const KiottySpecCursorResult all_steps = KiottySpec_openCheckpointCursor(context, static_cast<KiottySpecStep>(0));
    passed = expectTrue(ok(all_steps.code), "step filter 0 is all steps") && passed;
    KiottySpec_closeCursor(context, all_steps.value);
    KiottySpecDiffQuery current {};
    current.from = 1;
    passed = expectTrue(KiottySpec_openTextDiffCursor(context, item, static_cast<KiottySpecTextField>(KIOTTY_SPEC_CHANGED_FIELD_BODY), &current).code == invalid,
                        "text field body") && passed;
    passed = expectTrue(KiottySpec_readItemTag(context, item, 99).code == invalid, "read tag past the model") && passed;
    passed = expectTrue(KiottySpec_formatCheckpointState(static_cast<KiottySpecCheckpointState>(0)).code == invalid, "state 0 name") && passed;
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
    const KiottySpecSizeResult read = KiottySpec_readItemBody(other.value, textOf("REQ-001"), KiottySpecBuffer{body, sizeof(body)});
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

// 모델의 태그·관계 정의는 커서를 열고 고정 크기 페이지로 읽고 닫는다.
bool testModelDefinitionCursors(KiottySpecContext* context)
{
    const KiottySpecModel design = KiottySpec_readModel(context, KIOTTY_SPEC_ITEM_KIND_DESIGN).value;
    const KiottySpecCursorResult tags = KiottySpec_openTagDefinitionCursor(context, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    bool passed = expectTrue(ok(tags.code) && tags.total == design.tag_count && design.tag_count >= 2, "open tag definition cursor");
    const std::unique_ptr<KiottySpecTagDefinitionPage> tag_page = std::make_unique<KiottySpecTagDefinitionPage>();
    const KiottySpecStatus tag_fetched = KiottySpec_fetchTagDefinitionPage(context, tags.value, tag_page.get()).code;
    passed = expectTrue(ok(tag_fetched) && tag_page->length == design.tag_count && tag_page->elements[0].index == 0 &&
                            tag_page->elements[1].index == 1 && tag_page->elements[0].name[0] != 0 && tag_page->elements[0].bit_names[0][0] != 0,
                        "tag definition page") && passed;
    passed = expectTrue(ok(KiottySpec_fetchTagDefinitionPage(context, tags.value, tag_page.get()).code) && tag_page->length == 0, "end of tag definitions") &&
             passed;
    const std::unique_ptr<KiottySpecRelationDefinitionPage> relation_page = std::make_unique<KiottySpecRelationDefinitionPage>();
    passed = expectTrue(KiottySpec_fetchRelationDefinitionPage(context, tags.value, relation_page.get()).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE,
                        "tag definition cursor type") && passed;
    passed = expectTrue(KiottySpec_fetchTagDefinitionPage(context, tags.value, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null tag definition page") &&
             passed;
    passed = expectTrue(ok(KiottySpec_closeCursor(context, tags.value).code), "close tag definition cursor") && passed;
    passed = expectTrue(KiottySpec_fetchTagDefinitionPage(context, tags.value, tag_page.get()).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "closed tag definition cursor") &&
             passed;
    const KiottySpecCursorResult relations = KiottySpec_openRelationDefinitionCursor(context, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    passed = expectTrue(ok(relations.code) && relations.total == design.relation_count && design.relation_count >= 2, "open relation definition cursor") && passed;
    const KiottySpecStatus relation_fetched = KiottySpec_fetchRelationDefinitionPage(context, relations.value, relation_page.get()).code;
    passed = expectTrue(ok(relation_fetched) && relation_page->length == design.relation_count && relation_page->elements[1].index == 1 &&
                            relation_page->elements[0].name[0] != 0 && ok(KiottySpec_formatKind(relation_page->elements[0].target_kind).code),
                        "relation definition page") && passed;
    passed = expectTrue(ok(KiottySpec_fetchRelationDefinitionPage(context, relations.value, relation_page.get()).code) && relation_page->length == 0,
                        "end of relation definitions") && passed;
    passed = expectTrue(KiottySpec_fetchTagDefinitionPage(context, relations.value, tag_page.get()).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE,
                        "relation definition cursor type") && passed;
    passed = expectTrue(ok(KiottySpec_closeCursor(context, relations.value).code), "close relation definition cursor") && passed;
    const KiottySpecModel milestone = KiottySpec_readModel(context, KIOTTY_SPEC_ITEM_KIND_MILESTONE).value;
    const KiottySpecCursorResult empty = KiottySpec_openTagDefinitionCursor(context, KIOTTY_SPEC_ITEM_KIND_MILESTONE);
    passed = expectTrue(milestone.tag_count == 0 && ok(empty.code) && empty.total == 0, "open empty tag definition cursor") && passed;
    passed = expectTrue(ok(KiottySpec_fetchTagDefinitionPage(context, empty.value, tag_page.get()).code) && tag_page->length == 0, "empty tag definition page") &&
             passed;
    KiottySpec_closeCursor(context, empty.value);
    const KiottySpecItemKind invalid = static_cast<KiottySpecItemKind>(KIOTTY_SPEC_ITEM_KIND_COUNT + 1);
    passed = expectTrue(KiottySpec_openTagDefinitionCursor(context, invalid).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "invalid tag definition kind") && passed;
    passed = expectTrue(KiottySpec_openRelationDefinitionCursor(context, static_cast<KiottySpecItemKind>(0)).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "invalid relation definition kind") && passed;
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
    const KiottySpecIdResult given = createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("DSN-010"), textOf("주어진 ID"), textOf(""));
    passed = expectTrue(ok(given.code) && std::strcmp(given.value, "DSN-010") == 0, "given id") && passed;
    const KiottySpecIdResult issued = createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("발급"), textOf(""));
    passed = expectTrue(ok(issued.code) && std::strcmp(issued.value, "DSN-011") == 0, "issued id skips past given id") && passed;
    passed = expectTrue(createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("DSN-010"), textOf("겹침"), textOf("")).code ==
                            KIOTTY_SPEC_STATUS_CONFLICT,
                        "duplicate id rejected") &&
             passed;
    passed = expectTrue(createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf("_bad"), textOf("잘못"), textOf("")).code ==
                            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "invalid id rejected") &&
             passed;
    const KiottySpecIdResult requirement = createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구"), textOf(""));
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
    passed = expectTrue(ok(KiottySpec_readItemBody(context, textOf("LOGIN"), KiottySpecBuffer{body, sizeof(body)}).code) && std::strcmp(body, "본문") == 0,
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
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구 1"), textOf(""));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("요구 2"), textOf(""));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("본문 저장"), textOf("본문은 ID로만 읽는다"));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("파일 추적"), textOf(""));
    KiottySpec_addRelation(context, textOf("DSN-002"), 0, textOf("REQ-001"));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("링크 그래프"), textOf("관계를 링크 타입으로 검사한다"));
    KiottySpec_addRelation(context, textOf("DSN-003"), 0, textOf("REQ-001"));
    KiottySpec_setItemTag(context, KiottySpec_findItem(context, textOf("DSN-003")).value, 1, 1);
    bool passed = expectTrue(ok(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code), "complete first");

    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING, textOf("설계 2"));
    KiottySpec_removeItem(context, textOf("DSN-002"));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("커서 풀"), textOf(""));
    KiottySpec_addRelation(context, textOf("DSN-004"), 0, textOf("REQ-002"));
    KiottySpec_updateItem(context, textOf("DSN-003"), textOf("관계 저장소"), textOf("관계를 모델의 관계 정의로 검사한다"));
    KiottySpec_setItemTag(context, KiottySpec_findItem(context, textOf("DSN-003")).value, 1, 4);
    KiottySpec_removeRelation(context, textOf("DSN-003"), 0, textOf("REQ-001"));
    KiottySpec_addRelation(context, textOf("DSN-003"), 0, textOf("REQ-002"));
    KiottySpec_writeItemBody(context, textOf("DSN-001"), textOf("## 내용\n바뀜\n"));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, textOf(""), textOf("잠깐"), textOf(""));
    KiottySpec_removeItem(context, textOf("DSN-005"));

    KiottySpecDiffQuery query {1, KIOTTY_SPEC_CHECKPOINT_CURRENT, 1u << (KIOTTY_SPEC_ITEM_KIND_DESIGN - 1), 0};
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(context, &query);
    const KiottySpecKindDiff& design = summary.kinds[KIOTTY_SPEC_ITEM_KIND_DESIGN - 1];
    passed = expectTrue(ok(summary.code) && summary.to == 2, "summary range") && passed;
    passed = expectTrue(design.added == 1 && design.modified == 2 && design.removed == 1, "summary items") && passed;
    passed = expectTrue(design.relations_added == 2 && design.relations_removed == 2, "summary relations") && passed;
    passed = expectTrue(summary.kinds[KIOTTY_SPEC_ITEM_KIND_REQUIREMENT - 1].added == 0, "kind mask") && passed;

    const KiottySpecCursorResult items = KiottySpec_openItemDiffCursor(context, &query);
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
    passed = expectTrue(KiottySpec_openItemDiffCursor(context, &query).total == 3, "field mask keeps added and removed") && passed;
    query.field_mask = 0;

    const KiottySpecCursorResult relations = KiottySpec_openRelationDiffCursor(context, &query);
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
    passed = expectTrue(KiottySpec_readItemBodyAt(context, textOf("DSN-001"), 1, KiottySpecBuffer{body, sizeof(body)}).value == 0, "old body empty") && passed;

    const KiottySpecItemHandle handle = KiottySpec_findItem(context, textOf("DSN-003")).value;
    const KiottySpecDiffQuery text_range {1, 2, 0, 0};
    const KiottySpecCursorResult text = KiottySpec_openTextDiffCursor(context, handle, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, &text_range);
    passed = expectTrue(ok(text.code) && text.total == 4, "text diff pieces") && passed;
    const char* expected[] = {"관계를 ", "링크 타입으로", "모델의 관계 정의로", " 검사한다"};
    const KiottySpecTextChange changes[] = {KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED, KIOTTY_SPEC_TEXT_CHANGE_REMOVED, KIOTTY_SPEC_TEXT_CHANGE_ADDED, KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED};
    KiottySpecTextDiffPage text_page {};
    passed = expectTrue(ok(KiottySpec_fetchTextDiffPage(context, text.value, &text_page).code) && text_page.length == 4, "text diff page") && passed;
    for (size_t index = 0; index < 4 && index < text_page.length; ++index) {
        const KiottySpecTextDiff& piece = text_page.elements[index];
        passed = expectTrue(piece.change == changes[index] && piece.length == std::strlen(expected[index]) && std::strcmp(piece.text, expected[index]) == 0,
                            expected[index]) && passed;
    }
    passed = expectTrue(ok(KiottySpec_fetchTextDiffPage(context, text.value, &text_page).code) && text_page.length == 0, "end of pieces") && passed;
    KiottySpec_closeCursor(context, text.value);
    passed = expectTrue(KiottySpec_fetchTextDiffPage(context, text.value, &text_page).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "closed cursor") && passed;

    passed = expectTrue(KiottySpec_openTextDiffCursor(context, handle, static_cast<KiottySpecTextField>(KIOTTY_SPEC_CHANGED_FIELD_BODY), &text_range).code ==
                            KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "body rejected") && passed;
    passed = expectTrue(KiottySpec_readLastError(context).value.subject == KIOTTY_SPEC_SUBJECT_TEXT_FIELD, "body rejected names the field") && passed;
    passed = expectTrue(KiottySpec_openTextDiffCursor(context, KIOTTY_SPEC_INVALID_ITEM_HANDLE, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, &text_range).code ==
                            KIOTTY_SPEC_STATUS_INVALID_HANDLE, "invalid handle") && passed;
    passed = expectTrue(KiottySpec_openTextDiffCursor(context, handle, KIOTTY_SPEC_TEXT_FIELD_SUMMARY, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "null range") && passed;
    const KiottySpecCursorResult wrong = KiottySpec_openItemDiffCursor(context, &query);
    passed = expectTrue(KiottySpec_fetchTextDiffPage(context, wrong.value, &text_page).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "wrong cursor type") && passed;
    KiottySpec_closeCursor(context, wrong.value);
    passed = expectTrue(KiottySpec_summarizeDiff(context, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null query") && passed;
    passed = expectTrue(KiottySpec_openItemDiffCursor(context, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null item diff query") && passed;
    passed = expectTrue(KiottySpec_openRelationDiffCursor(context, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null relation diff query") && passed;
    query.from = 2;
    query.to = 1;
    passed = expectTrue(KiottySpec_summarizeDiff(context, &query).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "reversed range") && passed;
    query.from = 9;
    query.to = KIOTTY_SPEC_CHECKPOINT_CURRENT;
    passed = expectTrue(KiottySpec_summarizeDiff(context, &query).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing checkpoint") && passed;
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
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("a"), textOf(""));
    const KiottySpecIdResult second = createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("b"), textOf(""));
    const KiottySpecIdResult full = createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("c"), textOf(""));
    passed = expectTrue(full.code == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "third item does not fit") && passed;
    passed = expectTrue(ok(KiottySpec_removeItem(context, textOf(second.value)).code), "remove second") && passed;
    const KiottySpecIdResult again = createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("d"), textOf(""));
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
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("REQ-001"), textOf("a"), textOf(""));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf("REQ-002"), textOf("b"), textOf(""));
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("aaaaaa")).code), "first body") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-002"), textOf("bbbbbb")).code), "second body") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("cccccccc")).code), "rewrite compacts") && passed;
    passed = expectTrue(KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("ddddddddddd")).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "body area full") && passed;
    char body[32] {};
    KiottySpec_readItemBody(context, textOf("REQ-001"), KiottySpecBuffer{body, sizeof(body)});
    passed = expectTrue(std::strcmp(body, "cccccccc") == 0, "failed body write rolled back") && passed;
    KiottySpec_readItemBody(context, textOf("REQ-002"), KiottySpecBuffer{body, sizeof(body)});
    passed = expectTrue(std::strcmp(body, "bbbbbb") == 0, "other body kept") && passed;
    passed = expectTrue(ok(KiottySpec_writeItemBody(context, textOf("REQ-002"), textOf("")).code), "empty body") && passed;
    const KiottySpecSizeResult empty = KiottySpec_readItemBody(context, textOf("REQ-002"), KiottySpecBuffer{body, sizeof(body)});
    passed = expectTrue(ok(empty.code) && empty.value == 0 && body[0] == '\0', "empty body reads empty") && passed;
    KiottySpec_releaseContext(context);
    std::filesystem::remove_all(root, error);
    return passed;
}

KiottySpecText textOf(const std::string& text) { return KiottySpecText{text.data(), text.size()}; }

// 개발수칙 2.10 의 다섯 입력: 빈 입력, 최대 길이, 최대 길이 + 1, 중간 null 바이트, 잘못된 UTF-8
struct BoundaryInputs {
    std::string empty;
    std::string longest;
    std::string too_long;
    std::string with_nul;
    std::string broken;
};

BoundaryInputs boundaryInputs(size_t max_length, char fill)
{
    BoundaryInputs inputs {};
    inputs.longest = std::string(max_length, fill);
    inputs.too_long = std::string(max_length + 1, fill);
    inputs.with_nul = std::string("A") + std::string(1, '\0') + std::string("B");
    inputs.broken = std::string("A") + std::string(1, static_cast<char>(0xFF)) + std::string("B");
    return inputs;
}

bool expectRejected(KiottySpecContext* context, KiottySpecStatus code, KiottySpecStatus expected, KiottySpecSubject subject, const std::string& label)
{
    const KiottySpecErrorResult error = KiottySpec_readLastError(context);
    const bool rejected = code == expected && error.value.status == expected && error.value.subject == subject;
    return expectTrue(rejected, label.c_str());
}

using Call = std::function<KiottySpecStatus(KiottySpecText)>;

// empty 는 빈 입력의 기대 상태다. 빈 입력을 경계가 받아들여도 도메인이 거부할 수 있다.
bool checkBoundary(KiottySpecContext* context, const std::string& name, size_t max_length, char fill, KiottySpecSubject subject, KiottySpecStatus empty,
                   const Call& call)
{
    const BoundaryInputs inputs = boundaryInputs(max_length, fill);
    const KiottySpecStatus empty_code = call(textOf(inputs.empty));
    bool passed = expectTrue(empty_code == empty, (name + " empty").c_str());
    if (!ok(empty)) { passed = expectRejected(context, empty_code, empty, subject, name + " empty subject") && passed; }
    passed = expectTrue(call(textOf(inputs.longest)) != KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, (name + " max length").c_str()) && passed;
    passed = expectRejected(context, call(textOf(inputs.too_long)), KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, subject, name + " max length + 1") && passed;
    passed = expectRejected(context, call(textOf(inputs.with_nul)), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, subject, name + " nul byte") && passed;
    passed = expectRejected(context, call(textOf(inputs.broken)), KIOTTY_SPEC_STATUS_INVALID_UTF8, subject, name + " invalid utf-8") && passed;
    return passed;
}

KiottySpecContextResult openProject(const std::string& root)
{
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    KiottySpecConfiguration configuration {};
    return openContext(configuration, root);
}

bool testParseBoundaries()
{
    std::error_code error;
    const std::string root = (std::filesystem::temp_directory_path(error) / "kiotty_api_parse").generic_string();
    const KiottySpecContextResult created = openProject(root);
    KiottySpecContext* const context = created.value;
    bool passed = expectTrue(ok(created.code), "open parse project");
    passed = expectTrue(ok(KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("경계")).code), "start") && passed;
    const KiottySpecText none {nullptr, 0};
    const KiottySpecItemKind requirement = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;

    passed = checkBoundary(context, "id", KIOTTY_SPEC_MAX_ID_LENGTH, 'A', KIOTTY_SPEC_SUBJECT_ID, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText id) { return createItem(context, requirement, id, textOf("t"), none).code; }) && passed;
    passed = checkBoundary(context, "title", KIOTTY_SPEC_MAX_TITLE_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_TITLE, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                           [&](KiottySpecText title) { return createItem(context, requirement, none, title, none).code; }) && passed;
    passed = checkBoundary(context, "summary", KIOTTY_SPEC_MAX_SUMMARY_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_SUMMARY, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText summary) { return createItem(context, requirement, none, textOf("t"), summary).code; }) && passed;
    passed = checkBoundary(context, "body", KIOTTY_SPEC_MAX_BODY_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_BODY, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText body) { return KiottySpec_writeItemBody(context, textOf("REQ-001"), body).code; }) && passed;
    passed = checkBoundary(context, "decision", KIOTTY_SPEC_MAX_DECISION_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_DECISION, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText text) { return KiottySpec_writeDecision(context, requirement, text).code; }) && passed;
    passed = checkBoundary(context, "description", KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_DESCRIPTION, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText text) { return KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, text).code; }) &&
             passed;
    passed = checkBoundary(context, "project name", KIOTTY_SPEC_MAX_PROJECT_NAME_LENGTH, 'a', KIOTTY_SPEC_SUBJECT_PROJECT_NAME, KIOTTY_SPEC_STATUS_SUCCESS,
                           [&](KiottySpecText name) { return KiottySpec_initializeProject(context, name).code; }) && passed;
    passed = expectTrue(KiottySpec_updateItem(context, textOf("REQ-001"), KiottySpecText{nullptr, 3}, none).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                        "null data with length") && passed;
    passed = expectTrue(ok(KiottySpec_updateItem(context, textOf("REQ-001"), none, none).code), "null data leaves fields unchanged") && passed;
    KiottySpec_releaseContext(context);

    const std::string base = root + "/p";
    std::string longest = base;
    while (longest.size() < 1024 - std::strlen("/x.kisp")) { longest += longest.size() % 100 == 0 ? '/' : 'a'; }
    longest += "/x.kisp";
    KiottySpecConfiguration configuration {};
    const Call open = [&](KiottySpecText path) {
        const KiottySpecContextResult other = KiottySpec_createContext(&configuration);
        const KiottySpecStatus code = KiottySpec_open(other.value, path).code;
        const KiottySpecErrorResult last = KiottySpec_readLastError(other.value);
        KiottySpec_releaseContext(other.value);
        g_last_error = last.value;
        return code;
    };
    const BoundaryInputs inputs = boundaryInputs(longest.size(), 'a');
    const auto pathRejected = [&](KiottySpecStatus code, KiottySpecStatus expected, const char* label) {
        return expectTrue(code == expected && g_last_error.status == expected && g_last_error.subject == KIOTTY_SPEC_SUBJECT_FILE, label);
    };
    passed = pathRejected(open(textOf(inputs.empty)), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "path empty") && passed;
    passed = expectTrue(open(textOf(longest)) != KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED || g_last_error.subject != KIOTTY_SPEC_SUBJECT_FILE, "path max length") && passed;
    passed = pathRejected(open(textOf(longest + "a")), KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "path max length + 1") && passed;
    passed = pathRejected(open(textOf(root + std::string(1, '\0') + "/x.kisp")), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "path nul byte") && passed;
    passed = pathRejected(open(textOf(root + inputs.broken + "/x.kisp")), KIOTTY_SPEC_STATUS_INVALID_UTF8, "path invalid utf-8") && passed;
    std::filesystem::remove_all(root, error);
    return passed;
}

// header 뒤에 lines 를 붙인 .kisp 를 연다. 실패 이유는 g_last_error 에 남긴다.
KiottySpecStatus openKispLines(const std::string& root, const std::string& lines)
{
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root + "/.kiotty", error);
    {
        std::ofstream file(root + "/.kiotty/spec.kisp", std::ios::binary);
        file << "#kisp 2 model 1\n" << lines;
    }
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult opened = openContext(configuration, root);
    g_last_error = KiottySpec_readLastError(opened.value).value;
    KiottySpec_releaseContext(opened.value);
    return opened.code;
}

bool expectLineRejected(KiottySpecStatus code, const char* label)
{
    const bool rejected = code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && g_last_error.status == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT &&
                          g_last_error.subject == KIOTTY_SPEC_SUBJECT_FILE_LINE;
    return expectTrue(rejected, label);
}

// 개발수칙 2.10: SpecParser::parse 는 .kisp 파일의 각 줄을 받는 경계다.
bool testSpecLineBoundaries()
{
    std::error_code error;
    const std::string root = (std::filesystem::temp_directory_path(error) / "kiotty_api_lines").generic_string();
    const BoundaryInputs description = boundaryInputs(KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH, 'a');
    const auto checkpointLine = [](const std::string& text) { return "checkpoint\tO\t1\t-\t2\tstarted\t0\t" + text + "\n"; };
    bool passed = expectTrue(ok(openKispLines(root, "")), "header only");
    passed = expectTrue(ok(openKispLines(root, "\n")), "empty line is skipped") && passed;
    passed = expectLineRejected(openKispLines(root, "\t\n"), "empty fields") && passed;
    passed = expectTrue(ok(openKispLines(root, checkpointLine(description.empty))), "description empty") && passed;
    passed = expectTrue(ok(openKispLines(root, checkpointLine(description.longest))), "description max length") && passed;
    passed = expectLineRejected(openKispLines(root, checkpointLine(description.too_long)), "description max length + 1") && passed;
    passed = expectLineRejected(openKispLines(root, checkpointLine(description.with_nul)), "description nul byte") && passed;
    passed = expectLineRejected(openKispLines(root, checkpointLine(description.broken)), "description invalid utf-8") && passed;
    const BoundaryInputs value = boundaryInputs(255, 'a');
    const auto metaLine = [](const std::string& text) { return "meta\tO\tproject.name\t" + text + "\n"; };
    passed = expectTrue(ok(openKispLines(root, metaLine(value.empty))), "meta value empty") && passed;
    passed = expectTrue(ok(openKispLines(root, metaLine(value.longest))), "meta value max length") && passed;
    passed = expectLineRejected(openKispLines(root, metaLine(value.too_long)), "meta value max length + 1") && passed;
    passed = expectLineRejected(openKispLines(root, metaLine(value.with_nul)), "meta value nul byte") && passed;
    passed = expectLineRejected(openKispLines(root, metaLine(value.broken)), "meta value invalid utf-8") && passed;
    passed = expectLineRejected(openKispLines(root, "meta\tO\t" + value.broken + "\tx\n"), "meta key invalid utf-8") && passed;
    std::filesystem::remove_all(root, error);
    return passed;
}

bool testConfiguration()
{
    KiottySpecConfiguration configuration {};
    configuration.max_cursors = KIOTTY_SPEC_MAX_CURSOR_CAPACITY + 1;
    bool passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "too many cursors");
    configuration = KiottySpecConfiguration{};
    configuration.max_items = KIOTTY_SPEC_MAX_POOL_CAPACITY + 1;
    passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "too many items") && passed;
    configuration = KiottySpecConfiguration{};
    configuration.max_cursor_rows = KIOTTY_SPEC_MAX_POOL_CAPACITY + 1;
    passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "too many cursor rows") && passed;
    configuration = KiottySpecConfiguration{};
    configuration.work_buffer_bytes = KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES - 1;
    passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "work buffer too small") && passed;
    configuration.work_buffer_bytes = KIOTTY_SPEC_MAX_BUFFER_BYTES + 1;
    passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "work buffer too large") && passed;
    configuration = KiottySpecConfiguration{};
    configuration.max_body_bytes = KIOTTY_SPEC_MAX_BUFFER_BYTES + 1;
    passed = expectTrue(KiottySpec_createContext(&configuration).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "body area too large") && passed;
    configuration = KiottySpecConfiguration{};
    configuration.work_buffer_bytes = KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES;
    configuration.max_cursors = 1;
    configuration.max_cursor_rows = 1;
    const KiottySpecContextResult smallest = KiottySpec_createContext(&configuration);
    passed = expectTrue(ok(smallest.code), "smallest configuration") && passed;
    const KiottySpecErrorResult nothing = KiottySpec_readLastError(smallest.value);
    passed = expectTrue(ok(nothing.code) && nothing.value.status == KIOTTY_SPEC_STATUS_SUCCESS && nothing.value.subject == KIOTTY_SPEC_SUBJECT_NONE &&
                            nothing.value.text[0] == '\0' && nothing.value.line == 0, "no error yet") && passed;
    KiottySpec_releaseContext(smallest.value);
    passed = expectTrue(KiottySpec_createContext(nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null configuration") && passed;
    passed = expectTrue(KiottySpec_readLastError(nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null context error") && passed;
    return passed;
}

bool testNames()
{
    const KiottySpecNameResult status_name = KiottySpec_formatStatus(KIOTTY_SPEC_STATUS_INVALID_UTF8);
    bool passed = expectTrue(ok(status_name.code) && std::strcmp(status_name.value.value, "INVALID_UTF8") == 0, "status name");
    const KiottySpecNameResult unknown_status = KiottySpec_formatStatus(static_cast<KiottySpecStatus>(99));
    passed = expectTrue(unknown_status.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && unknown_status.value.value[0] == '\0', "unknown status") && passed;
    const KiottySpecNameResult kind_name = KiottySpec_formatKind(KIOTTY_SPEC_ITEM_KIND_DESIGN);
    passed = expectTrue(ok(kind_name.code) && std::strcmp(kind_name.value.value, "design") == 0, "kind name") && passed;
    const KiottySpecNameResult unknown_kind = KiottySpec_formatKind(static_cast<KiottySpecItemKind>(0));
    passed = expectTrue(unknown_kind.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && unknown_kind.value.value[0] == '\0', "unknown kind") && passed;
    const KiottySpecNameResult state_name = KiottySpec_formatCheckpointState(KIOTTY_SPEC_CHECKPOINT_STATE_DONE);
    passed = expectTrue(ok(state_name.code) && std::strcmp(state_name.value.value, "done") == 0, "state name") && passed;
    const KiottySpecNameResult unknown_state = KiottySpec_formatCheckpointState(static_cast<KiottySpecCheckpointState>(7));
    passed = expectTrue(unknown_state.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && unknown_state.value.value[0] == '\0', "unknown state") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf("design")) == KIOTTY_SPEC_ITEM_KIND_DESIGN, "parse kind") && passed;
    passed = expectTrue(KiottySpec_parseKind(KiottySpecText{nullptr, 4}) == 0, "parse kind rejects null data") && passed;
    const BoundaryInputs inputs = boundaryInputs(KIOTTY_SPEC_MAX_NAME_LENGTH, 'a');
    passed = expectTrue(KiottySpec_parseKind(textOf(inputs.empty)) == 0, "parse kind empty") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf(inputs.longest)) == 0, "parse kind max length is unknown") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf("code_inspection_result")) == KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT, "parse longest kind") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf(inputs.too_long)) == 0, "parse kind max length + 1") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf(std::string("design") + std::string(1, '\0'))) == 0, "parse kind nul byte") && passed;
    passed = expectTrue(KiottySpec_parseKind(textOf(std::string("design") + inputs.broken)) == 0, "parse kind invalid utf-8") && passed;
    return passed;
}

// 확정 조건이 어긋나면 completeCheckpoint 는 COMPLETION 이고, 어긋난 조건은 커서로 읽는다.
bool testUnmetConditions()
{
    std::error_code error;
    const std::string root = (std::filesystem::temp_directory_path(error) / "kiotty_api_unmet").generic_string();
    const KiottySpecContextResult created = openProject(root);
    KiottySpecContext* const context = created.value;
    bool passed = expectTrue(ok(created.code), "open unmet project");
    passed = expectTrue(KiottySpec_openUnmetConditionCursor(context).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "no checkpoint yet") && passed;
    KiottySpec_startCheckpoint(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING, textOf("요구"));
    createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("기능"), textOf(""));
    KiottySpec_setItemTag(context, KiottySpec_findItem(context, textOf("REQ-001")).value, 0, 1);
    passed = expectTrue(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code == KIOTTY_SPEC_STATUS_PRECONDITION, "unmet complete") && passed;
    const KiottySpecErrorResult last = KiottySpec_readLastError(context);
    passed = expectTrue(last.value.status == KIOTTY_SPEC_STATUS_PRECONDITION && last.value.subject == KIOTTY_SPEC_SUBJECT_COMPLETION, "completion error") &&
             passed;
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(context);
    passed = expectTrue(ok(cursor.code) && cursor.total == 1, "one unmet condition") && passed;
    KiottySpecUnmetConditionPage page {};
    passed = expectTrue(ok(KiottySpec_fetchUnmetConditionPage(context, cursor.value, &page).code) && page.length == 1 &&
                            page.elements[0].condition == KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA && std::strcmp(page.elements[0].id, "REQ-001") == 0,
                        "unmet condition row") && passed;
    KiottySpecItemPage items {};
    passed = expectTrue(KiottySpec_fetchItemPage(context, cursor.value, &items).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "unmet cursor type") && passed;
    passed = expectTrue(ok(KiottySpec_closeCursor(context, cursor.value).code), "close unmet cursor") && passed;
    passed = expectTrue(KiottySpec_fetchUnmetConditionPage(context, cursor.value, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null unmet page") &&
             passed;
    KiottySpec_writeItemBody(context, textOf("REQ-001"), textOf("## 인수 조건\n- 된다\n"));
    const KiottySpecCursorResult met = KiottySpec_openUnmetConditionCursor(context);
    passed = expectTrue(ok(met.code) && met.total == 0, "conditions met") && passed;
    KiottySpec_closeCursor(context, met.value);
    passed = expectTrue(ok(KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code), "complete after fix") && passed;
    passed = expectTrue(createItem(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, textOf(""), textOf("t"), textOf("")).code == KIOTTY_SPEC_STATUS_CONFIRMED,
                        "done rejects writes") && passed;
    passed = expectTrue(KiottySpec_createItem(context, nullptr).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "null draft") && passed;
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
        {"value boundaries", testValueBoundaries},
        {"query boundaries", testQueryBoundaries},
        {"complete", testComplete},
        {"reopen", testReopen},
        {"outside change", testOutsideChange},
        {"diff", testDiff},
        {"given id and rename", testGivenIdAndRename},
        {"model definition cursors", testModelDefinitionCursors},
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
    const std::pair<const char*, bool (*)()> standalone[] = {
        {"parse boundaries", testParseBoundaries},
        {"spec line boundaries", testSpecLineBoundaries},
        {"configuration", testConfiguration},
        {"names", testNames},
        {"unmet conditions", testUnmetConditions},
    };
    for (const auto& test : standalone) {
        const bool passed = test.second();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
