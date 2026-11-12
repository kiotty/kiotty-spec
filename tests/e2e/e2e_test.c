/* E2E 테스트: 빌드된 라이브러리를 C 프로그램이 include/ 의 헤더만으로 구동한다.
 * E2E-001 ~ E2E-019 가 외부 인터페이스 묶음 UI-001 ~ UI-019 를 하나씩 검증한다 (CR-013).
 * C 로 컴파일하므로 C 호출자가 보는 상수 · 열거형 · 구조체 배치도 함께 검증한다. */

#include "kiotty_spec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OK KIOTTY_SPEC_STATUS_SUCCESS
#define PATH_CAPACITY 512

#define BASE_CAPACITY 256

static char g_base[BASE_CAPACITY];

/* ------------------------------------------------------------------ helpers */

static int check(int condition, const char* label)
{
    if (!condition) { printf("  FAIL: %s\n", label); }
    return condition ? 1 : 0;
}

static KiottySpecText text(const char* value)
{
    KiottySpecText result;
    result.data = value;
    result.length = value == NULL ? 0 : strlen(value);
    return result;
}

static KiottySpecText noText(void)
{
    KiottySpecText result;
    result.data = NULL;
    result.length = 0;
    return result;
}

static void makePath(char* target, const char* scenario, const char* leaf)
{
    const int written = snprintf(target, PATH_CAPACITY, "%s/%s/.kiotty/%s", g_base, scenario, leaf);
    if (written < 0 || written >= PATH_CAPACITY) { target[0] = '\0'; }
}

static int fileExists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) { return 0; }
    fclose(file);
    return 1;
}

/* 새 임시 폴더의 사양 파일을 연 컨텍스트 */
static KiottySpecContext* openSpec(const char* scenario)
{
    KiottySpecConfiguration configuration;
    char path[PATH_CAPACITY];
    memset(&configuration, 0, sizeof(configuration));
    KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    if (created.code != OK) { return NULL; }
    makePath(path, scenario, "spec.kisp");
    if (KiottySpec_open(created.value, text(path)).code != OK) {
        KiottySpec_releaseContext(created.value);
        return NULL;
    }
    return created.value;
}

static KiottySpecCheckpointHandle start(KiottySpecContext* context, KiottySpecStep step)
{
    return KiottySpec_startCheckpoint(context, step, text("e2e")).value;
}

static KiottySpecIdResult add(KiottySpecContext* context, KiottySpecItemKind kind, const char* id,
                              const char* title)
{
    KiottySpecItemDraft draft;
    memset(&draft, 0, sizeof(draft));
    draft.kind = kind;
    draft.id = text(id);
    draft.title = text(title);
    draft.summary = noText();
    return KiottySpec_createItem(context, &draft);
}

static KiottySpecItemHandle find(KiottySpecContext* context, const char* id)
{
    return KiottySpec_findItem(context, text(id)).value;
}

static KiottySpecStatus complete(KiottySpecContext* context)
{
    return KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_USER).code;
}

static KiottySpecBuffer buffer(char* data, size_t capacity)
{
    KiottySpecBuffer result;
    result.data = data;
    result.capacity = capacity;
    return result;
}

static KiottySpecDiffQuery range(KiottySpecCheckpointHandle from, KiottySpecCheckpointHandle to)
{
    KiottySpecDiffQuery query;
    memset(&query, 0, sizeof(query));
    query.from = from;
    query.to = to;
    return query;
}

static const char* ACCEPTED_BODY = "## 인수 조건\n- 저장된다\n";
static const char* OLD_TITLE = "관계를 링크 타입으로 검사한다";
static const char* NEW_TITLE = "관계를 모델의 관계 정의로 검사한다";

/* CP A: REQ-001(기능, 인수 조건, 태그 1) · REQ-002, 결정 "처음" → 확정.
 * CP B: REQ-001 제목 · 태그 3, 결정 "나중", UI-001 → REQ-001 관계. 돌려주는 값은 CP A. */
static KiottySpecCheckpointHandle buildHistory(KiottySpecContext* context)
{
    const KiottySpecCheckpointHandle first = start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", OLD_TITLE);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-002", "둘째");
    KiottySpec_setItemTag(context, find(context, "REQ-001"), 0, 1);
    KiottySpec_writeItemBody(context, text("REQ-001"), text(ACCEPTED_BODY));
    KiottySpec_writeDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, text("처음"));
    complete(context);
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    KiottySpec_updateItem(context, text("REQ-001"), text(NEW_TITLE), noText());
    KiottySpec_setItemTag(context, find(context, "REQ-001"), 0, 3);
    KiottySpec_writeDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, text("나중"));
    add(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-001", "열기");
    KiottySpec_addRelation(context, text("UI-001"), 0, text("REQ-001"));
    return first;
}

/* ------------------------------------------------------------------ E2E-001 컨텍스트와 사양 파일 */

static int e2e001(void)
{
    int passed = 1;
    KiottySpecConfiguration configuration;
    char path[PATH_CAPACITY];
    memset(&configuration, 0, sizeof(configuration));
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    passed &= check(created.code == OK && created.value != NULL, "1 기본 설정으로 컨텍스트를 만든다");
    passed &= check(KiottySpec_createContext(NULL).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "2 설정 NULL 은 INVALID_ARGUMENT");
    if (created.value == NULL) { return 0; }
    KiottySpecContext* context = created.value;
    const KiottySpecStatus early = KiottySpec_initializeProject(context, text("e2e")).code;
    const KiottySpecErrorResult reason = KiottySpec_readLastError(context);
    passed &= check(early != OK && reason.code == OK && reason.value.status == early,
                    "3 열기 전 초기화는 실패하고 실패 이유의 status 가 같다");
    makePath(path, "e2e001", "spec.kisp");
    passed &= check(KiottySpec_open(context, text(path)).code == OK && fileExists(path),
                    "4 없는 폴더 아래 경로를 열면 사양 파일이 만들어진다");
    passed &= check(KiottySpec_initializeProject(context, text("e2e")).code == OK, "5 프로젝트 초기화");
    const KiottySpecItemResult missing = KiottySpec_readItem(context, text("REQ-999"));
    const KiottySpecErrorResult last = KiottySpec_readLastError(context);
    const int terminated = memchr(last.value.text, '\0', sizeof(last.value.text)) != NULL;
    passed &= check(missing.code == KIOTTY_SPEC_STATUS_NOT_FOUND &&
                        last.value.status == KIOTTY_SPEC_STATUS_NOT_FOUND && terminated &&
                        strstr(last.value.text, "REQ-999") != NULL,
                    "6 실패 이유에 NOT_FOUND 와 그 ID, null 종료");
    passed &= check(strcmp(KiottySpec_formatStatus(OK).value.value, "SUCCESS") == 0, "7 SUCCESS 이름");
    KiottySpec_releaseContext(context);
    KiottySpec_releaseContext(NULL);
    passed &= check(1, "8 해제 (NULL 도 안전)");
    return passed;
}

/* ------------------------------------------------------------------ E2E-002 이름 표 */

static int e2e002(void)
{
    int passed = 1;
    passed &= check(strcmp(KiottySpec_formatKind(KIOTTY_SPEC_ITEM_KIND_DESIGN).value.value, "design") == 0,
                    "1 DESIGN 이름 design");
    passed &= check(KiottySpec_parseKind(text("design")) == KIOTTY_SPEC_ITEM_KIND_DESIGN, "2 design → DESIGN");
    passed &= check((int)KiottySpec_parseKind(text("nope")) == 0, "3 없는 이름은 0");
    passed &= check(strcmp(KiottySpec_formatCheckpointState(KIOTTY_SPEC_CHECKPOINT_STATE_DONE).value.value,
                           "done") == 0,
                    "4 DONE 이름 done");
    const KiottySpecNameResult unknown = KiottySpec_formatKind((KiottySpecItemKind)0);
    passed &= check(unknown.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT && unknown.value.value[0] == '\0',
                    "5 Kind 0 은 INVALID_ARGUMENT, 빈 이름");
    return passed;
}

/* ------------------------------------------------------------------ E2E-003 항목 */

static int e2e003(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e003");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const KiottySpecIdResult first = add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "", "저장");
    passed &= check(first.code == OK && strcmp(first.value, "REQ-001") == 0, "1 ID 없이 만들면 REQ-001");
    const KiottySpecIdResult given = add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-100", "둘");
    const KiottySpecIdResult again = add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-100", "셋");
    passed &= check(given.code == OK && again.code == KIOTTY_SPEC_STATUS_CONFLICT, "2 같은 ID 는 CONFLICT");
    passed &= check(add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "A B", "x").code ==
                        KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "3 ID \"A B\" 는 INVALID_ARGUMENT");
    const KiottySpecItemResult by_id = KiottySpec_readItem(context, text("REQ-001"));
    const KiottySpecItemResult by_handle = KiottySpec_readItemByHandle(context, find(context, "REQ-001"));
    passed &= check(by_id.code == OK && by_handle.code == OK && strcmp(by_id.value.id, by_handle.value.id) == 0 &&
                        strcmp(by_id.value.title, by_handle.value.title) == 0 &&
                        strcmp(by_id.value.summary, by_handle.value.summary) == 0,
                    "4 ID 로 읽은 것과 핸들로 읽은 것이 같다");
    KiottySpec_updateItem(context, text("REQ-001"), text("새 제목"), noText());
    passed &= check(strcmp(KiottySpec_readItem(context, text("REQ-001")).value.title, "새 제목") == 0,
                    "5 제목을 고친다");
    KiottySpec_setItemParent(context, text("REQ-100"), text("REQ-001"));
    passed &= check(KiottySpec_removeItem(context, text("REQ-001")).code == KIOTTY_SPEC_STATUS_PRECONDITION,
                    "6 하위가 있는 항목은 지울 수 없다");
    char small[4];
    char large[256];
    KiottySpec_writeItemBody(context, text("REQ-001"), text(ACCEPTED_BODY));
    const KiottySpecSizeResult short_read = KiottySpec_readItemBody(context, text("REQ-001"), buffer(small, sizeof(small)));
    const KiottySpecSizeResult full_read = KiottySpec_readItemBody(context, text("REQ-001"), buffer(large, sizeof(large)));
    passed &= check(short_read.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED && short_read.value >= strlen(ACCEPTED_BODY) &&
                        full_read.code == OK && strcmp(large, ACCEPTED_BODY) == 0,
                    "7 작은 버퍼는 CAPACITY_EXCEEDED 와 필요한 크기, 큰 버퍼는 같은 본문");
    const KiottySpecStatus renamed = KiottySpec_renameItem(context, text("REQ-100"), text("REQ-200")).code;
    const KiottySpecStatus found = KiottySpec_findItem(context, text("REQ-200")).code;
    const KiottySpecStatus removed = KiottySpec_removeItem(context, text("REQ-200")).code;
    passed &= check(renamed == OK && found == OK && removed == OK &&
                        KiottySpec_findItem(context, text("REQ-200")).code == KIOTTY_SPEC_STATUS_NOT_FOUND,
                    "8 ID 를 바꾸고 지우면 NOT_FOUND");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-004 태그 */

static int e2e004(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e004");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "저장");
    const KiottySpecItemHandle item = find(context, "REQ-001");
    passed &= check(KiottySpec_setItemTag(context, item, 0, 1).code == OK &&
                        KiottySpec_readItemTag(context, item, 0).value.value == 1,
                    "1 태그 0 에 1 을 쓰고 읽는다");
    passed &= check(KiottySpec_setItemTag(context, item, 0, (uint64_t)1 << 5).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "2 이름 없는 비트는 INVALID_ARGUMENT");
    passed &= check(KiottySpec_setItemTag(context, item, 99, 1).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "3 모델에 없는 태그 index 는 INVALID_ARGUMENT");
    const KiottySpecCursorResult cursor = KiottySpec_openTagCursor(context, item);
    KiottySpecTagPage* page = (KiottySpecTagPage*)calloc(1, sizeof(KiottySpecTagPage));
    const KiottySpecStatus fetched = KiottySpec_fetchTagPage(context, cursor.value, page).code;
    passed &= check(cursor.code == OK && cursor.total == 1 && fetched == OK && page->length == 1 &&
                        page->elements[0].item == item && page->elements[0].tag == 0 &&
                        page->elements[0].value == 1 && page->elements[0].model == KIOTTY_SPEC_ITEM_KIND_REQUIREMENT,
                    "4 태그 커서에 (항목, 태그 0, 값 1)");
    KiottySpec_closeCursor(context, cursor.value);
    free(page);
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-005 관계 */

static int e2e005(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e005");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-001", "열기");
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "저장");
    passed &= check(KiottySpec_addRelation(context, text("UI-001"), 0, text("REQ-001")).code == OK, "1 관계를 만든다");
    passed &= check(KiottySpec_addRelation(context, text("UI-001"), 0, text("REQ-001")).code == KIOTTY_SPEC_STATUS_CONFLICT,
                    "2 같은 관계는 CONFLICT");
    passed &= check(KiottySpec_addRelation(context, text("REQ-001"), 0, text("UI-001")).code ==
                        KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "3 모델에 없는 관계는 INVALID_ARGUMENT");
    KiottySpecRelationPage* page = (KiottySpecRelationPage*)calloc(1, sizeof(KiottySpecRelationPage));
    const KiottySpecCursorResult by_item = KiottySpec_openRelationCursor(context, text("UI-001"), KIOTTY_SPEC_DIRECTION_BOTH);
    KiottySpec_fetchRelationPage(context, by_item.value, page);
    const int item_ok = by_item.total == 1 && page->length == 1 && page->elements[0].to == find(context, "REQ-001");
    KiottySpec_closeCursor(context, by_item.value);
    const KiottySpecCursorResult by_model =
        KiottySpec_openRelationCursorByModel(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, 0);
    KiottySpec_fetchRelationPage(context, by_model.value, page);
    const int model_ok = by_model.total == 1 && page->length == 1 && page->elements[0].to == find(context, "REQ-001");
    KiottySpec_closeCursor(context, by_model.value);
    passed &= check(item_ok && model_ok, "4 항목 · 모델 관계 커서 모두 대상 REQ-001");
    passed &= check(KiottySpec_removeRelation(context, text("UI-001"), 0, text("REQ-001")).code == OK &&
                        KiottySpec_removeRelation(context, text("UI-001"), 0, text("REQ-001")).code ==
                            KIOTTY_SPEC_STATUS_NOT_FOUND,
                    "5 끊고 다시 끊으면 NOT_FOUND");
    free(page);
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-006 항목 목록과 커서 */

static int e2e006(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e006");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    for (int index = 0; index < 70; ++index) { add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "설계"); }
    KiottySpecItemPage* page = (KiottySpecItemPage*)calloc(1, sizeof(KiottySpecItemPage));
    const KiottySpecCursorResult designs = KiottySpec_openItemCursor(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, NULL, 0);
    size_t lengths[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        KiottySpec_fetchItemPage(context, designs.value, page);
        lengths[index] = page->length;
    }
    KiottySpec_closeCursor(context, designs.value);
    passed &= check(designs.total == 70 && lengths[0] == 64 && lengths[1] == 6 && lengths[2] == 0,
                    "1 설계 70 개는 페이지 64 · 6 · 0");
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "기능");
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-002", "태그 없음");
    KiottySpec_setItemTag(context, find(context, "REQ-001"), 0, 1);
    KiottySpecTagQuery query;
    memset(&query, 0, sizeof(query));
    query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
    query.tag_index = 0;
    query.mask = 1;
    const KiottySpecCursorResult tagged = KiottySpec_openItemCursor(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, &query, 1);
    KiottySpec_fetchItemPage(context, tagged.value, page);
    passed &= check(tagged.total == 1 && page->length == 1 && strcmp(page->elements[0].id, "REQ-001") == 0,
                    "2 태그 조건에 맞는 하나만");
    KiottySpec_closeCursor(context, tagged.value);
    KiottySpec_setItemParent(context, text("DSN-002"), text("DSN-001"));
    KiottySpec_setItemParent(context, text("DSN-003"), text("DSN-001"));
    const KiottySpecCursorResult children = KiottySpec_openChildCursor(context, text("DSN-001"));
    passed &= check(children.total == 2, "3 하위 둘");
    KiottySpec_closeCursor(context, children.value);
    add(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "UI-001", "열기");
    KiottySpec_addRelation(context, text("UI-001"), 0, text("REQ-001"));
    KiottySpecIdPage ids;
    memset(&ids, 0, sizeof(ids));
    const KiottySpecCursorResult impact = KiottySpec_openImpactCursor(context, text("REQ-001"), 1);
    KiottySpec_fetchIdPage(context, impact.value, &ids);
    passed &= check(ids.length == 1 && strcmp(ids.elements[0], "UI-001") == 0, "4 영향 ID 에 UI-001");
    KiottySpec_closeCursor(context, impact.value);
    passed &= check(KiottySpec_fetchIdPage(context, impact.value, &ids).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE,
                    "5 닫은 커서는 INVALID_HANDLE");
    free(page);
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-007 체크포인트 */

static int hasUnmet(KiottySpecContext* context, const char* id, KiottySpecCondition condition)
{
    KiottySpecUnmetConditionPage page;
    memset(&page, 0, sizeof(page));
    const KiottySpecCursorResult cursor = KiottySpec_openUnmetConditionCursor(context);
    KiottySpec_fetchUnmetConditionPage(context, cursor.value, &page);
    KiottySpec_closeCursor(context, cursor.value);
    for (size_t index = 0; index < page.length; ++index) {
        if (strcmp(page.elements[index].id, id) == 0 && page.elements[index].condition == condition) { return 1; }
    }
    return 0;
}

static int e2e007(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e007");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecCheckpointHandle first = start(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    const KiottySpecCheckpointResult started = KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    passed &= check(first == 1 && started.handle == 1 && started.value.state == KIOTTY_SPEC_CHECKPOINT_STATE_STARTED,
                    "1 Step 4 를 시작하면 CP 1, STARTED");
    add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "설계");
    passed &= check(KiottySpec_readCheckpoint(context, first).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
                    "2 첫 쓰기에 RUNNING");
    const KiottySpecStatus by_ai = KiottySpec_completeCheckpoint(context, KIOTTY_SPEC_ACTOR_AI).code;
    const KiottySpecStatus by_user = complete(context);
    passed &= check(by_ai == KIOTTY_SPEC_STATUS_PERMISSION && by_user == OK &&
                        KiottySpec_readCheckpoint(context, first).value.state == KIOTTY_SPEC_CHECKPOINT_STATE_DONE,
                    "3 AI 는 PERMISSION, 사용자는 DONE");
    passed &= check(add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "늦은 설계").code == KIOTTY_SPEC_STATUS_CONFIRMED,
                    "4 DONE 뒤 쓰기는 CONFIRMED");
    const KiottySpecCheckpointHandle second = start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "기능");
    KiottySpec_setItemTag(context, find(context, "REQ-001"), 0, 1);
    passed &= check(complete(context) == KIOTTY_SPEC_STATUS_PRECONDITION &&
                        hasUnmet(context, "REQ-001", KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA),
                    "5 인수 조건이 없으면 PRECONDITION, 어긋난 조건에 REQ-001");
    KiottySpec_writeItemBody(context, text("REQ-001"), text(ACCEPTED_BODY));
    complete(context);
    start(context, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    const KiottySpecCursorResult all = KiottySpec_openCheckpointCursor(context, (KiottySpecStep)0);
    const KiottySpecCursorResult design = KiottySpec_openCheckpointCursor(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    passed &= check(all.total == 3 && design.total == 1, "6 전체 3, Step 4 는 1");
    KiottySpec_closeCursor(context, all.value);
    KiottySpec_closeCursor(context, design.value);
    const KiottySpecSizeResult pruned = KiottySpec_pruneCheckpoints(context, KIOTTY_SPEC_ACTOR_USER, second);
    const KiottySpecSizeResult high = KiottySpec_pruneCheckpoints(context, KIOTTY_SPEC_ACTOR_USER, ((uint64_t)1 << 32) | second);
    passed &= check(pruned.code == OK && pruned.value == 1 && high.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "7 CP 2 기준 정리로 1 개를 지우고, 상위 32 비트가 있으면 INVALID_ARGUMENT");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-008 결정 기록 */

static int e2e008(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e008");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    const char* decision = "- 파일 하나로 둔다";
    char read[128];
    char small[4];
    KiottySpec_writeDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, text(decision));
    const KiottySpecSizeResult full = KiottySpec_readDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, buffer(read, sizeof(read)));
    passed &= check(full.code == OK && strcmp(read, decision) == 0, "1 쓰고 읽으면 같은 글");
    const KiottySpecSizeResult cut = KiottySpec_readDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, buffer(small, sizeof(small)));
    passed &= check(cut.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED && cut.value >= strlen(decision),
                    "2 작은 버퍼는 CAPACITY_EXCEEDED 와 필요한 크기");
    passed &= check(KiottySpec_writeDecision(context, KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST, text("- x")).code ==
                        KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "3 결정 기록이 없는 Kind 는 INVALID_ARGUMENT");
    char* longest = (char*)malloc(KIOTTY_SPEC_MAX_DECISION_LENGTH + 2);
    memset(longest, 'a', KIOTTY_SPEC_MAX_DECISION_LENGTH + 1);
    longest[KIOTTY_SPEC_MAX_DECISION_LENGTH + 1] = '\0';
    passed &= check(KiottySpec_writeDecision(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, text(longest)).code ==
                        KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
                    "4 상한 + 1 은 CAPACITY_EXCEEDED");
    free(longest);
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-009 모델 */

static int e2e009(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e009");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecModelResult model = KiottySpec_readModel(context, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    passed &= check(model.code == OK && strcmp(model.value.name, "설계 사항") == 0 && strcmp(model.value.id_prefix, "DSN") == 0 &&
                        model.value.tag_count == 2 && model.value.relation_count == 3,
                    "1 DESIGN 모델: 설계 사항, DSN, 태그 2, 관계 3");
    KiottySpecTagDefinitionPage* tags = (KiottySpecTagDefinitionPage*)calloc(1, sizeof(KiottySpecTagDefinitionPage));
    const KiottySpecCursorResult tag_cursor = KiottySpec_openTagDefinitionCursor(context, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    KiottySpec_fetchTagDefinitionPage(context, tag_cursor.value, tags);
    passed &= check(tag_cursor.total == 2 && tags->length == 2 && strcmp(tags->elements[0].name, "설계 종류") == 0,
                    "2 태그 정의 2 개, 첫 이름 설계 종류");
    KiottySpec_closeCursor(context, tag_cursor.value);
    free(tags);
    KiottySpecRelationDefinitionPage* relations =
        (KiottySpecRelationDefinitionPage*)calloc(1, sizeof(KiottySpecRelationDefinitionPage));
    const KiottySpecCursorResult relation_cursor =
        KiottySpec_openRelationDefinitionCursor(context, KIOTTY_SPEC_ITEM_KIND_DESIGN);
    KiottySpec_fetchRelationDefinitionPage(context, relation_cursor.value, relations);
    passed &= check(relation_cursor.total == 3 && relations->length == 3 &&
                        relations->elements[0].target_kind == KIOTTY_SPEC_ITEM_KIND_REQUIREMENT &&
                        relations->elements[1].target_kind == KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE &&
                        relations->elements[2].target_kind == KIOTTY_SPEC_ITEM_KIND_DESIGN,
                    "3 관계 정의 3 개, 대상 Kind 가 모델대로");
    KiottySpec_closeCursor(context, relation_cursor.value);
    free(relations);
    const KiottySpecItemKind none = (KiottySpecItemKind)0;
    passed &= check(KiottySpec_readModel(context, none).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT &&
                        KiottySpec_openTagDefinitionCursor(context, none).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT &&
                        KiottySpec_openRelationDefinitionCursor(context, none).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "4 Kind 0 은 INVALID_ARGUMENT");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-010 현재 위치 */

static int e2e010(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e010");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecPosition empty = KiottySpec_readPosition(context).value;
    passed &= check(empty.current == 0 && empty.next_step == KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED,
                    "1 체크포인트가 없으면 다음 단계 Step 1");
    start(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    for (int index = 0; index < 3; ++index) { add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "설계"); }
    const KiottySpecPosition running = KiottySpec_readPosition(context).value;
    passed &= check(running.step == KIOTTY_SPEC_STEP_DESIGN_DISCUSSING && running.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING &&
                        running.pending_writes == 3,
                    "2 Step 4, RUNNING, 묶인 쓰기 3");
    KiottySpec_releaseContext(context);
    context = openSpec("e2e010_runs");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    start(context, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    KiottySpecStepRunPage page;
    memset(&page, 0, sizeof(page));
    const KiottySpecCursorResult runs = KiottySpec_openStepRunCursor(context);
    KiottySpec_fetchStepRunPage(context, runs.value, &page);
    KiottySpec_closeCursor(context, runs.value);
    int counted = page.length == 2;
    for (size_t index = 0; counted && index < page.length; ++index) {
        const KiottySpecStepRun* run = &page.elements[index];
        const int requirement = run->step == KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING && run->count == 2;
        const int interface = run->step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING && run->count == 1;
        counted = requirement || interface;
    }
    passed &= check(counted, "3 단계 구간: Step 2 (2), Step 3 (1)");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-011 체크포인트 비교 */

static int e2e011(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e011");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecCheckpointHandle first = buildHistory(context);
    const KiottySpecDiffQuery query = range(first, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(context, &query);
    const KiottySpecKindDiff* requirement = &summary.kinds[KIOTTY_SPEC_ITEM_KIND_REQUIREMENT - 1];
    const KiottySpecKindDiff* interface = &summary.kinds[KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE - 1];
    uint32_t relations_added = 0;
    for (int index = 0; index < KIOTTY_SPEC_ITEM_KIND_COUNT; ++index) { relations_added += summary.kinds[index].relations_added; }
    passed &= check(summary.code == OK && requirement->modified == 1 && requirement->decision_changed == KIOTTY_SPEC_TRUE &&
                        interface->added == 1 && relations_added == 1,
                    "1 요구사항 수정 1 · 결정 바뀜, 외부 인터페이스 추가 1, 관계 추가 1");
    KiottySpecItemDiffPage* items = (KiottySpecItemDiffPage*)calloc(1, sizeof(KiottySpecItemDiffPage));
    const KiottySpecCursorResult item_cursor = KiottySpec_openItemDiffCursor(context, &query);
    KiottySpec_fetchItemDiffPage(context, item_cursor.value, items);
    int modified = 0;
    for (size_t index = 0; index < items->length; ++index) {
        const KiottySpecItemDiff* diff = &items->elements[index];
        if (strcmp(diff->id, "REQ-001") == 0 && diff->change == KIOTTY_SPEC_CHANGE_MODIFIED &&
            (diff->changed_fields & KIOTTY_SPEC_CHANGED_FIELD_TITLE) != 0) {
            modified = 1;
        }
    }
    passed &= check(modified, "2 REQ-001 이 MODIFIED, 바뀐 필드에 TITLE");
    KiottySpec_closeCursor(context, item_cursor.value);
    free(items);
    KiottySpecRelationDiffPage* relations = (KiottySpecRelationDiffPage*)calloc(1, sizeof(KiottySpecRelationDiffPage));
    const KiottySpecCursorResult relation_cursor = KiottySpec_openRelationDiffCursor(context, &query);
    KiottySpec_fetchRelationDiffPage(context, relation_cursor.value, relations);
    passed &= check(relations->length == 1 && relations->elements[0].change == KIOTTY_SPEC_CHANGE_ADDED &&
                        strcmp(relations->elements[0].from_id, "UI-001") == 0 && strcmp(relations->elements[0].to_id, "REQ-001") == 0,
                    "3 관계 UI-001 → REQ-001 이 ADDED");
    KiottySpec_closeCursor(context, relation_cursor.value);
    free(relations);
    const KiottySpecCheckpointHandle latest = KiottySpec_readCheckpoint(context, KIOTTY_SPEC_CHECKPOINT_CURRENT).handle;
    const KiottySpecDiffQuery reversed = range(latest, first);
    passed &= check(KiottySpec_summarizeDiff(context, &reversed).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "4 거꾸로 된 구간은 INVALID_ARGUMENT");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-012 지난 시점 읽기 */

static int e2e012(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e012");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecCheckpointHandle first = buildHistory(context);
    passed &= check(strcmp(KiottySpec_readItemAt(context, text("REQ-001"), first).value.title, OLD_TITLE) == 0,
                    "1 그 시점의 제목");
    passed &= check(KiottySpec_readItemTagAt(context, text("REQ-001"), 0, first).value.value == 1, "2 그 시점의 태그 값 1");
    char body[256];
    const KiottySpecSizeResult read = KiottySpec_readItemBodyAt(context, text("REQ-001"), first, buffer(body, sizeof(body)));
    passed &= check(read.code == OK && strcmp(body, ACCEPTED_BODY) == 0, "3 그 시점의 본문 (지금과 같음)");
    char decision[64];
    const KiottySpecSizeResult old = KiottySpec_readDecisionAt(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, first,
                                                               buffer(decision, sizeof(decision)));
    passed &= check(old.code == OK && strcmp(decision, "처음") == 0, "4 그 시점의 결정 기록");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-013 글자 차이 */

static int e2e013(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e013");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecCheckpointHandle first = buildHistory(context);
    const KiottySpecDiffQuery query = range(first, KIOTTY_SPEC_CHECKPOINT_CURRENT);
    const KiottySpecItemHandle item = find(context, "REQ-001");
    KiottySpecTextDiffPage* page = (KiottySpecTextDiffPage*)calloc(1, sizeof(KiottySpecTextDiffPage));
    const KiottySpecCursorResult cursor = KiottySpec_openTextDiffCursor(context, item, KIOTTY_SPEC_TEXT_FIELD_TITLE, &query);
    KiottySpec_fetchTextDiffPage(context, cursor.value, page);
    const char* expected[4] = {"관계를 ", "링크 타입으로", "모델의 관계 정의로", " 검사한다"};
    const KiottySpecTextChange changes[4] = {KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED, KIOTTY_SPEC_TEXT_CHANGE_REMOVED,
                                             KIOTTY_SPEC_TEXT_CHANGE_ADDED, KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED};
    int pieces = cursor.code == OK && page->length == 4;
    for (size_t index = 0; pieces && index < 4; ++index) {
        pieces = strcmp(page->elements[index].text, expected[index]) == 0 && page->elements[index].change == changes[index] &&
                 page->elements[index].length == strlen(expected[index]);
    }
    passed &= check(pieces, "1 같음 · 지움 · 더함 · 같음 조각");
    KiottySpec_closeCursor(context, cursor.value);
    free(page);
    passed &= check(KiottySpec_openTextDiffCursor(context, item, (KiottySpecTextField)KIOTTY_SPEC_CHANGED_FIELD_BODY, &query).code ==
                        KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "2 BODY 필드는 INVALID_ARGUMENT");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-014 상태 코드와 열거형 */

static int e2e014(void)
{
    int passed = 1;
    passed &= check(KIOTTY_SPEC_STATUS_SUCCESS == 0 && sizeof(KiottySpecStatus) == 4 && sizeof(KiottySpecItemKind) == 4 &&
                        sizeof(KiottySpecSubject) == 4,
                    "1 SUCCESS 0, enum 4 바이트");
    passed &= check(KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST == 1 && KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_RESULT == 13 &&
                        KIOTTY_SPEC_ITEM_KIND_COUNT == 13 && KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED == 1 &&
                        KIOTTY_SPEC_STEP_END_TO_END_TEST_RUN == 12 && KIOTTY_SPEC_CHECKPOINT_STATE_STARTED == 1 &&
                        KIOTTY_SPEC_CHECKPOINT_STATE_DONE == 3 && KIOTTY_SPEC_ACTOR_USER == 1 && KIOTTY_SPEC_ACTOR_AI == 2,
                    "1 Kind · 단계 · 상태 · actor 는 1 부터 문서 순서");
    KiottySpecContext* context = openSpec("e2e014");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecStatus code = KiottySpec_findItem(context, text("REQ-999")).code;
    const KiottySpecError reason = KiottySpec_readLastError(context).value;
    passed &= check(code == KIOTTY_SPEC_STATUS_NOT_FOUND && reason.subject == KIOTTY_SPEC_SUBJECT_ITEM &&
                        strcmp(KiottySpec_formatStatus(code).value.value, "NOT_FOUND") == 0,
                    "2 실패 상태 NOT_FOUND, subject ITEM, 이름 NOT_FOUND");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-015 핸들과 입력 값 */

static int e2e015(void)
{
    int passed = 1;
    passed &= check(sizeof(KiottySpecItemHandle) == 8 && sizeof(KiottySpecCheckpointHandle) == 8 &&
                        sizeof(KiottySpecCursor) == 8 && sizeof(KiottySpecBool) == 4,
                    "1 핸들 · 커서 8 바이트, Bool 4 바이트");
    KiottySpecContext* context = openSpec("e2e015");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    const KiottySpecIdResult empty_summary = add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", "요약 없음");
    passed &= check(empty_summary.code == OK &&
                        KiottySpec_readItem(context, text(empty_summary.value)).value.summary[0] == '\0',
                    "2 data NULL, 길이 0 요약은 빈 요약");
    KiottySpecItemDraft draft;
    memset(&draft, 0, sizeof(draft));
    draft.kind = KIOTTY_SPEC_ITEM_KIND_DESIGN;
    draft.title.data = NULL;
    draft.title.length = 1;
    passed &= check(KiottySpec_createItem(context, &draft).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                    "3 data NULL, 길이 1 제목은 INVALID_ARGUMENT");
    passed &= check(KiottySpec_readItemByHandle(context, KIOTTY_SPEC_INVALID_ITEM_HANDLE).code != OK,
                    "4 INVALID_ITEM_HANDLE 로 읽기는 실패");
    KiottySpec_writeItemBody(context, text(empty_summary.value), text(ACCEPTED_BODY));
    const KiottySpecSizeResult measured = KiottySpec_readItemBody(context, text(empty_summary.value), buffer(NULL, 0));
    passed &= check(measured.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED && measured.value >= strlen(ACCEPTED_BODY),
                    "5 data NULL 버퍼는 CAPACITY_EXCEEDED 와 필요한 크기");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-016 레코드 */

#define SIZE_IS(type, size) check(sizeof(type) == (size), "크기 " #type)

static int e2e016(void)
{
    int passed = 1;
    passed &= SIZE_IS(KiottySpecError, 272) & SIZE_IS(KiottySpecName, 32) & SIZE_IS(KiottySpecItem, 424) &
              SIZE_IS(KiottySpecTag, 24) & SIZE_IS(KiottySpecRelation, 24) & SIZE_IS(KiottySpecCheckpoint, 88) &
              SIZE_IS(KiottySpecStepRun, 32) & SIZE_IS(KiottySpecModel, 64) & SIZE_IS(KiottySpecTagDefinition, 3144) &
              SIZE_IS(KiottySpecRelationDefinition, 76) & SIZE_IS(KiottySpecTagQuery, 16) & SIZE_IS(KiottySpecPosition, 40) &
              SIZE_IS(KiottySpecUnmetCondition, 24) & SIZE_IS(KiottySpecDiffQuery, 24) & SIZE_IS(KiottySpecItemDiff, 56) &
              SIZE_IS(KiottySpecRelationDiff, 56) & SIZE_IS(KiottySpecKindDiff, 24) & SIZE_IS(KiottySpecTextDiff, 264);
    KiottySpecContext* context = openSpec("e2e016");
    if (!check(context != NULL, "열기")) { return 0; }
    const KiottySpecCheckpointHandle checkpoint = start(context, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "저장");
    KiottySpecItemDraft draft;
    memset(&draft, 0, sizeof(draft));
    draft.kind = KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE;
    draft.id = text("UI-001");
    draft.title = text("열기");
    draft.summary = text("사양 파일을 연다");
    KiottySpec_createItem(context, &draft);
    KiottySpec_setItemParent(context, text("UI-001"), text("REQ-001"));
    KiottySpec_setItemTag(context, find(context, "UI-001"), 0, 2);
    KiottySpec_addRelation(context, text("UI-001"), 0, text("REQ-001"));
    const KiottySpecItem item = KiottySpec_readItem(context, text("UI-001")).value;
    passed &= check(strcmp(item.id, "UI-001") == 0 && strcmp(item.title, "열기") == 0 && strcmp(item.summary, "사양 파일을 연다") == 0 &&
                        item.kind == KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE && item.checkpoint == checkpoint &&
                        item.parent == find(context, "REQ-001"),
                    "1 항목 레코드의 필드가 쓴 값과 같다");
    const KiottySpecTag tag = KiottySpec_readItemTag(context, find(context, "UI-001"), 0).value;
    passed &= check(tag.item == find(context, "UI-001") && tag.value == 2 && tag.model == KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE &&
                        tag.tag == 0,
                    "1 태그 레코드의 필드가 쓴 값과 같다");
    KiottySpecRelationPage* relations = (KiottySpecRelationPage*)calloc(1, sizeof(KiottySpecRelationPage));
    const KiottySpecCursorResult cursor = KiottySpec_openRelationCursorByModel(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, 0);
    KiottySpec_fetchRelationPage(context, cursor.value, relations);
    passed &= check(relations->length == 1 && relations->elements[0].from == find(context, "UI-001") &&
                        relations->elements[0].to == find(context, "REQ-001") &&
                        relations->elements[0].model == KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE && relations->elements[0].relation == 0,
                    "1 관계 레코드의 필드가 쓴 값과 같다");
    KiottySpec_closeCursor(context, cursor.value);
    free(relations);
    const KiottySpecCheckpoint record = KiottySpec_readCheckpoint(context, checkpoint).value;
    passed &= check(strcmp(record.description, "e2e") == 0 && record.step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING &&
                        record.state == KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING && record.previous == 0 && record.created_at > 0,
                    "1 체크포인트 레코드의 필드가 쓴 값과 같다");
    const KiottySpecPosition position = KiottySpec_readPosition(context).value;
    passed &= check(position.current == checkpoint && position.step == KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING &&
                        position.pending_writes > 0,
                    "1 위치 레코드의 필드가 쓴 값과 같다");
    KiottySpec_findItem(context, text("REQ-999"));
    const KiottySpecError reason = KiottySpec_readLastError(context).value;
    passed &= check(memchr(reason.text, '\0', sizeof(reason.text)) != NULL && reason.line == 0,
                    "2 실패 이유 레코드: null 종료, line 0");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-017 페이지 */

static int e2e017(void)
{
    int passed = 1;
    passed &= check(KIOTTY_SPEC_PAGE_CAPACITY == 64, "1 페이지 용량 64");
    passed &= check(sizeof(KiottySpecItemPage) <= 27144 && sizeof(KiottySpecIdPage) <= 1032 &&
                        sizeof(KiottySpecTagDefinitionPage) <= 201224 && sizeof(KiottySpecTextDiffPage) <= 16904,
                    "1 페이지 크기가 C++ 과 같은 상한 안");
    KiottySpecContext* context = openSpec("e2e017");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "저장");
    for (int index = 0; index < 65; ++index) {
        const KiottySpecIdResult created = add(context, KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE, "", "열기");
        KiottySpec_addRelation(context, text(created.value), 0, text("REQ-001"));
    }
    KiottySpecIdPage first;
    KiottySpecIdPage second;
    KiottySpecIdPage third;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&third, 0, sizeof(third));
    const KiottySpecCursorResult cursor = KiottySpec_openImpactCursor(context, text("REQ-001"), 1);
    KiottySpec_fetchIdPage(context, cursor.value, &first);
    KiottySpec_fetchIdPage(context, cursor.value, &second);
    KiottySpec_fetchIdPage(context, cursor.value, &third);
    KiottySpec_closeCursor(context, cursor.value);
    int unique = 1;
    for (size_t index = 0; unique && index < first.length; ++index) {
        unique = second.length == 1 && strcmp(first.elements[index], second.elements[0]) != 0;
    }
    passed &= check(cursor.total == 65 && first.length == 64 && second.length == 1 && third.length == 0 && unique,
                    "2 ID 페이지 64 · 1 · 0, 겹치지 않는다");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-018 결과 구조체 */

static int e2e018(void)
{
    int passed = 1;
    passed &= SIZE_IS(KiottySpecStatusResult, 4) & SIZE_IS(KiottySpecIdResult, 24) & SIZE_IS(KiottySpecNameResult, 40) &
              SIZE_IS(KiottySpecErrorResult, 280) & SIZE_IS(KiottySpecItemResult, 440) & SIZE_IS(KiottySpecItemHandleResult, 16) &
              SIZE_IS(KiottySpecTagResult, 32) & SIZE_IS(KiottySpecCheckpointResult, 104) &
              SIZE_IS(KiottySpecCheckpointHandleResult, 16) & SIZE_IS(KiottySpecPositionResult, 48) &
              SIZE_IS(KiottySpecModelResult, 72) & SIZE_IS(KiottySpecDiffSummaryResult, 336);
    KiottySpecContext* context = openSpec("e2e018");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING);
    add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "저장");
    const KiottySpecItemHandleResult found = KiottySpec_findItem(context, text("REQ-001"));
    passed &= check(found.code == OK && found.value != KIOTTY_SPEC_INVALID_ITEM_HANDLE, "1 성공 결과: code SUCCESS 와 값");
    const KiottySpecItemHandleResult missing = KiottySpec_findItem(context, text("REQ-999"));
    const KiottySpecIdResult rejected = add(context, KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, "REQ-001", "다시");
    const KiottySpecItemResult unread = KiottySpec_readItem(context, text("REQ-999"));
    passed &= check(missing.code == KIOTTY_SPEC_STATUS_NOT_FOUND && missing.value == KIOTTY_SPEC_INVALID_ITEM_HANDLE &&
                        rejected.code == KIOTTY_SPEC_STATUS_CONFLICT && rejected.value[0] == '\0' &&
                        unread.code == KIOTTY_SPEC_STATUS_NOT_FOUND && unread.value.id[0] == '\0',
                    "2 실패 결과: code 가 실패 코드, 값은 빈 값");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ E2E-019 용량 상수 */

static int e2e019(void)
{
    int passed = 1;
    KiottySpecContext* context = openSpec("e2e019");
    if (!check(context != NULL, "열기")) { return 0; }
    start(context, KIOTTY_SPEC_STEP_DESIGN_DISCUSSING);
    passed &= check(add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "ABCDEFGHIJKLMNO", "상한").code == OK &&
                        add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "ABCDEFGHIJKLMNOP", "상한 + 1").code != OK,
                    "1 ID 15 바이트는 성공, 16 바이트는 실패");
    char title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 2];
    memset(title, 'a', sizeof(title) - 1);
    title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1] = '\0';
    const KiottySpecStatus too_long = add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", title).code;
    title[KIOTTY_SPEC_MAX_TITLE_LENGTH] = '\0';
    const KiottySpecStatus longest = add(context, KIOTTY_SPEC_ITEM_KIND_DESIGN, "", title).code;
    passed &= check(longest == OK && too_long == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "2 제목 상한은 성공, + 1 은 CAPACITY_EXCEEDED");
    KiottySpecItem item;
    KiottySpecError reason;
    passed &= check(sizeof(item.id) == KIOTTY_SPEC_MAX_ID_LENGTH + 1 && sizeof(item.title) == KIOTTY_SPEC_MAX_TITLE_LENGTH + 1 &&
                        sizeof(item.summary) == KIOTTY_SPEC_MAX_SUMMARY_LENGTH + 1 &&
                        sizeof(reason.text) == KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH + 1,
                    "3 문자 배열은 상수 + 1 바이트");
    KiottySpec_releaseContext(context);
    return passed;
}

/* ------------------------------------------------------------------ main */

typedef struct Scenario {
    const char* id;
    const char* name;
    int (*run)(void);
} Scenario;

static const Scenario SCENARIOS[] = {
    {"E2E-001", "컨텍스트와 사양 파일", e2e001}, {"E2E-002", "이름 표", e2e002},
    {"E2E-003", "항목", e2e003},                {"E2E-004", "태그", e2e004},
    {"E2E-005", "관계", e2e005},                {"E2E-006", "항목 목록과 커서", e2e006},
    {"E2E-007", "체크포인트", e2e007},          {"E2E-008", "결정 기록", e2e008},
    {"E2E-009", "모델", e2e009},                {"E2E-010", "현재 위치", e2e010},
    {"E2E-011", "체크포인트 비교", e2e011},     {"E2E-012", "지난 시점 읽기", e2e012},
    {"E2E-013", "글자 차이", e2e013},           {"E2E-014", "상태 코드와 열거형", e2e014},
    {"E2E-015", "핸들과 입력 값", e2e015},      {"E2E-016", "레코드", e2e016},
    {"E2E-017", "페이지", e2e017},              {"E2E-018", "결과 구조체", e2e018},
    {"E2E-019", "용량 상수", e2e019},
};

int main(void)
{
    const char* temporary = getenv("TEMP");
    if (temporary == NULL) { temporary = getenv("TMPDIR"); }
    if (temporary == NULL) { temporary = "/tmp"; }
    snprintf(g_base, sizeof(g_base), "%s/kiotty_e2e_test_%lld", temporary, (long long)time(NULL));
    int failed = 0;
    const size_t count = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);
    for (size_t index = 0; index < count; ++index) {
        const int passed = SCENARIOS[index].run();
        printf("%s %s %s\n", SCENARIOS[index].id, passed ? "PASS" : "FAIL", SCENARIOS[index].name);
        if (!passed) { ++failed; }
    }
    printf("%d failed\n", failed);
    return failed;
}
