#ifndef KIOTTY_SPEC_H
#define KIOTTY_SPEC_H

/* Kiotty-Spec C 인터페이스. 설계서 v1 9절과 C 인터페이스 v2 3절의 형태 규칙을 따른다.
 * - 문자열 입력은 길이가 붙은 KiottySpecText 다. null 종료를 가정하지 않는다.
 * - 결과는 상태 코드를 담은 결과 구조체다. 실패 이유는 readLastError 로 읽는다.
 * - 레코드는 포인터가 없는 고정 크기 구조체다. 목록은 커서를 열고 페이지를 받아 온다.
 * - 본문 · 결정 기록 같은 큰 텍스트는 호출자 버퍼로 읽는다. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KIOTTY_SPEC_API
#define KIOTTY_SPEC_API
#endif

/* 9.1 용량 상수. 길이는 null 종료 문자를 뺀 UTF-8 바이트 수다. */
#define KIOTTY_SPEC_MAX_ID_LENGTH          15
#define KIOTTY_SPEC_MAX_TITLE_LENGTH       127
#define KIOTTY_SPEC_MAX_SUMMARY_LENGTH     255
#define KIOTTY_SPEC_MAX_BODY_LENGTH        16384
#define KIOTTY_SPEC_MAX_DECISION_LENGTH    2048
#define KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH 63
#define KIOTTY_SPEC_MAX_MODEL_NAME_LENGTH  31
#define KIOTTY_SPEC_MAX_TAG_NAME_LENGTH    63
#define KIOTTY_SPEC_TAG_BIT_COUNT          48
#define KIOTTY_SPEC_MAX_TAGS_PER_MODEL     256
#define KIOTTY_SPEC_MAX_TAG_QUERIES        4
#define KIOTTY_SPEC_PAGE_CAPACITY          64
#define KIOTTY_SPEC_ITEM_KIND_COUNT        13
#define KIOTTY_SPEC_STEP_COUNT             12
#define KIOTTY_SPEC_CHECKPOINT_CURRENT     0

typedef enum KiottySpecStatus {
    KIOTTY_SPEC_STATUS_SUCCESS = 0,
    KIOTTY_SPEC_STATUS_NOT_FOUND,
    KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
    KIOTTY_SPEC_STATUS_CONFIRMED,
    KIOTTY_SPEC_STATUS_PERMISSION,
    KIOTTY_SPEC_STATUS_PRECONDITION,
    KIOTTY_SPEC_STATUS_CONFLICT,
    KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE,
    KIOTTY_SPEC_STATUS_INPUT_OUTPUT,
    KIOTTY_SPEC_STATUS_DATABASE,
    KIOTTY_SPEC_STATUS_OUT_OF_RANGE,
    KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
    KIOTTY_SPEC_STATUS_ARITHMETIC_OVERFLOW,
    KIOTTY_SPEC_STATUS_POOL_IS_FULL,
    KIOTTY_SPEC_STATUS_INVALID_HANDLE,
    KIOTTY_SPEC_STATUS_INVALID_UTF8
} KiottySpecStatus;

typedef enum KiottySpecActor {
    KIOTTY_SPEC_ACTOR_USER = 1,
    KIOTTY_SPEC_ACTOR_AI
} KiottySpecActor;

/* 3.1. 값은 1부터이며 kind_mask 의 비트 (kind - 1) 이다. */
typedef enum KiottySpecItemKind {
    KIOTTY_SPEC_ITEM_KIND_CHANGE_REQUEST = 1,
    KIOTTY_SPEC_ITEM_KIND_REQUIREMENT,
    KIOTTY_SPEC_ITEM_KIND_RULE,
    KIOTTY_SPEC_ITEM_KIND_EXTERNAL_INTERFACE,
    KIOTTY_SPEC_ITEM_KIND_DESIGN,
    KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_PLAN,
    KIOTTY_SPEC_ITEM_KIND_MILESTONE,
    KIOTTY_SPEC_ITEM_KIND_UNIT_TEST_RESULT,
    KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_PLAN,
    KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST_RESULT,
    KIOTTY_SPEC_ITEM_KIND_CODE_INSPECTION_RESULT,
    KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_PLAN,
    KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST_RESULT
} KiottySpecItemKind;

/* 5.2 */
typedef enum KiottySpecStep {
    KIOTTY_SPEC_STEP_CHANGE_REQUEST_REPORTED = 1,
    KIOTTY_SPEC_STEP_REQUIREMENT_DISCUSSING,
    KIOTTY_SPEC_STEP_EXTERNAL_INTERFACE_DISCUSSING,
    KIOTTY_SPEC_STEP_DESIGN_DISCUSSING,
    KIOTTY_SPEC_STEP_UNIT_TEST_PLAN_DISCUSSING,
    KIOTTY_SPEC_STEP_DEVELOPMENT_PLAN_DISCUSSING,
    KIOTTY_SPEC_STEP_IMPLEMENTING,
    KIOTTY_SPEC_STEP_SYSTEM_TEST_PLAN_DISCUSSING,
    KIOTTY_SPEC_STEP_SYSTEM_TEST_RUN,
    KIOTTY_SPEC_STEP_CODE_INSPECTING,
    KIOTTY_SPEC_STEP_END_TO_END_TEST_DISCUSSING,
    KIOTTY_SPEC_STEP_END_TO_END_TEST_RUN
} KiottySpecStep;

/* 5.1 */
typedef enum KiottySpecCheckpointState {
    KIOTTY_SPEC_CHECKPOINT_STATE_STARTED = 1,
    KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
    KIOTTY_SPEC_CHECKPOINT_STATE_DONE
} KiottySpecCheckpointState;

typedef enum KiottySpecCardinality {
    KIOTTY_SPEC_CARDINALITY_MANY = 1,
    KIOTTY_SPEC_CARDINALITY_SINGLE
} KiottySpecCardinality;

/* 관계는 아래 단계가 위 단계를 가리킨다. UPSTREAM 은 이 항목이 주어(from)인 관계, DOWNSTREAM 은 목적어(to)인 관계다. */
typedef enum KiottySpecDirection {
    KIOTTY_SPEC_DIRECTION_UPSTREAM = 1,
    KIOTTY_SPEC_DIRECTION_DOWNSTREAM,
    KIOTTY_SPEC_DIRECTION_BOTH
} KiottySpecDirection;

typedef struct KiottySpecContext KiottySpecContext;

typedef uint64_t KiottySpecItemHandle;        /* generation << 32 | index    */
typedef uint64_t KiottySpecCheckpointHandle;  /* reserved   << 32 | sequence */
typedef uint64_t KiottySpecCursor;            /* generation << 32 | index + 1 */

#define KIOTTY_SPEC_INVALID_ITEM_HANDLE ((KiottySpecItemHandle)0xFFFFFFFFu)

typedef struct KiottySpecText {
    const char *data;
    size_t length;
} KiottySpecText;

/* 9.2 레코드 */
typedef struct KiottySpecItem {
    KiottySpecCheckpointHandle checkpoint;
    KiottySpecItemHandle       parent;
    KiottySpecItemKind         kind;
    uint32_t                   reserved;
    char                       id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char                       title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1];
    char                       summary[KIOTTY_SPEC_MAX_SUMMARY_LENGTH + 1];
} KiottySpecItem;                                /* 424 바이트 */

typedef struct KiottySpecTag {
    KiottySpecItemHandle item;
    KiottySpecItemKind   model;
    uint32_t             tag;
    uint64_t             value;
} KiottySpecTag;                                 /* 24 바이트 */

typedef struct KiottySpecRelation {
    KiottySpecItemHandle from;
    KiottySpecItemHandle to;
    KiottySpecItemKind   model;
    uint32_t             relation;
} KiottySpecRelation;                            /* 24 바이트 */

typedef struct KiottySpecCheckpoint {
    int64_t                    created_at;
    KiottySpecCheckpointHandle previous;
    KiottySpecStep             step;
    KiottySpecCheckpointState  state;
    char                       description[KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH + 1];
} KiottySpecCheckpoint;                          /* 88 바이트 */

/* 같은 단계가 이어진 체크포인트 구간. 정의서 4절 "지나온 단계" */
typedef struct KiottySpecStepRun {
    KiottySpecCheckpointHandle first;
    KiottySpecCheckpointHandle last;
    KiottySpecStep             step;
    KiottySpecCheckpointState  state;            /* last 의 상태 */
    uint64_t                   count;
} KiottySpecStepRun;

typedef struct KiottySpecModel {
    char                  name[KIOTTY_SPEC_MAX_MODEL_NAME_LENGTH + 1];
    char                  id_prefix[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecItemKind    kind;
    KiottySpecStep        created_in;
    uint32_t              tag_count;
    uint32_t              relation_count;
} KiottySpecModel;

typedef struct KiottySpecTagDefinition {
    char                  name[KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    char                  bit_names[KIOTTY_SPEC_TAG_BIT_COUNT][KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    KiottySpecCardinality cardinality;
} KiottySpecTagDefinition;                       /* 3140 바이트. 결과 구조체 대신 호출자 메모리에 채운다 */

typedef struct KiottySpecRelationDefinition {
    char                  name[KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    KiottySpecItemKind    target_kind;
    KiottySpecCardinality cardinality;
} KiottySpecRelationDefinition;                  /* 72 바이트 */

/* 3.4. 항목이 고르는 조건: TagRecord(model == kind, tag == tag_index) 가 있고 (value & mask) != 0 */
typedef struct KiottySpecTagQuery {
    KiottySpecItemKind kind;
    uint32_t           tag_index;
    uint64_t           mask;
} KiottySpecTagQuery;

/* 정의서 4절 "다시 돌아왔을 때". 문장은 ks 가 만든다. */
typedef struct KiottySpecPosition {
    KiottySpecCheckpointHandle current;          /* 체크포인트가 없으면 0 */
    KiottySpecStep             step;             /* current 의 단계. 없으면 0 */
    KiottySpecCheckpointState  state;            /* current 의 상태. 없으면 0 */
    KiottySpecStep             next_step;        /* 다음에 할 단계. 끝났으면 0 */
    uint64_t                   pending_writes;   /* current 가 RUNNING 일 때 거기에 묶인 쓰기 수 */
    uint64_t                   open_change_requests;
} KiottySpecPosition;

typedef struct KiottySpecItemPage       { size_t length; KiottySpecItem       elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecItemPage;
typedef struct KiottySpecTagPage        { size_t length; KiottySpecTag        elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecTagPage;
typedef struct KiottySpecRelationPage   { size_t length; KiottySpecRelation   elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecRelationPage;
typedef struct KiottySpecCheckpointPage { size_t length; KiottySpecCheckpointHandle handles[KIOTTY_SPEC_PAGE_CAPACITY]; KiottySpecCheckpoint elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecCheckpointPage;
typedef struct KiottySpecStepRunPage    { size_t length; KiottySpecStepRun    elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecStepRunPage;
typedef struct KiottySpecIdPage         { size_t length; char elements[KIOTTY_SPEC_PAGE_CAPACITY][KIOTTY_SPEC_MAX_ID_LENGTH + 1]; } KiottySpecIdPage;

/* 결과 구조체 (C 인터페이스 v2 3.3) */
typedef struct KiottySpecStatusResult           { KiottySpecStatus code; } KiottySpecStatusResult;
typedef struct KiottySpecSizeResult             { KiottySpecStatus code; size_t value; } KiottySpecSizeResult;
typedef struct KiottySpecIdResult               { KiottySpecStatus code; char value[KIOTTY_SPEC_MAX_ID_LENGTH + 1]; } KiottySpecIdResult;
typedef struct KiottySpecItemResult             { KiottySpecStatus code; KiottySpecItemHandle handle; KiottySpecItem value; } KiottySpecItemResult;
typedef struct KiottySpecItemHandleResult       { KiottySpecStatus code; KiottySpecItemHandle value; } KiottySpecItemHandleResult;
typedef struct KiottySpecTagResult              { KiottySpecStatus code; KiottySpecTag value; } KiottySpecTagResult;
typedef struct KiottySpecCheckpointResult       { KiottySpecStatus code; KiottySpecCheckpointHandle handle; KiottySpecCheckpoint value; } KiottySpecCheckpointResult;
typedef struct KiottySpecCheckpointHandleResult { KiottySpecStatus code; KiottySpecCheckpointHandle value; } KiottySpecCheckpointHandleResult;
typedef struct KiottySpecCursorResult           { KiottySpecStatus code; KiottySpecCursor value; size_t total; } KiottySpecCursorResult;
typedef struct KiottySpecPositionResult         { KiottySpecStatus code; KiottySpecPosition value; } KiottySpecPositionResult;
typedef struct KiottySpecModelResult            { KiottySpecStatus code; KiottySpecModel value; } KiottySpecModelResult;
typedef struct KiottySpecRelationDefinitionResult { KiottySpecStatus code; KiottySpecRelationDefinition value; } KiottySpecRelationDefinitionResult;
typedef struct KiottySpecContextResult          { KiottySpecStatus code; KiottySpecContext *value; } KiottySpecContextResult;

/* 0 인 용량은 기본값을 쓴다. */
typedef struct KiottySpecConfiguration {
    size_t         max_items;
    size_t         max_relations;
    size_t         max_tags;
    size_t         max_checkpoints;
    size_t         max_cursors;
    size_t         max_cursor_rows;
    size_t         work_buffer_bytes;
} KiottySpecConfiguration;

/* 컨텍스트. 힙 할당은 createContext 안에서만 한다. */
KIOTTY_SPEC_API KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration *configuration);
KIOTTY_SPEC_API void                    KiottySpec_releaseContext(KiottySpecContext *context);
/* .kisp 파일을 연다. 없으면 디렉토리와 함께 빈 파일을 만든다. 컨텍스트 하나는 파일 하나만 연다. 닫기는 releaseContext.
   .kisp 는 사람이 읽고 git 이 병합하는 텍스트이며 쓰기마다 전부 다시 쓴다. 본문은 확장자를 뺀 이름의 디렉토리에 둔다
   (.kiotty/spec.kisp → .kiotty/spec/<key>.md). 연 뒤에 파일이 밖에서 바뀌면 다음 쓰기는 CONFLICT 이고 다시 읽는다. */
KIOTTY_SPEC_API KiottySpecStatusResult  KiottySpec_open(KiottySpecContext *context, KiottySpecText path);
KIOTTY_SPEC_API KiottySpecSizeResult    KiottySpec_readLastError(KiottySpecContext *context, char *buffer, size_t capacity);
KIOTTY_SPEC_API const char             *KiottySpec_statusName(KiottySpecStatus status);
/* project_name 길이가 0 이면 프로젝트 루트(.kiotty 의 상위) 디렉토리 이름 */
KIOTTY_SPEC_API KiottySpecStatusResult  KiottySpec_initProject(KiottySpecContext *context, KiottySpecText project_name);

/* 이름 표. Kind 이름은 .kisp 와 ks 가 쓰는 영문 snake_case 다 ("design"). 모르는 이름이면 0. 한글 이름은 readModel 의 name */
KIOTTY_SPEC_API const char             *KiottySpec_kindName(KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecItemKind      KiottySpec_parseKind(KiottySpecText name);
KIOTTY_SPEC_API const char             *KiottySpec_checkpointStateName(KiottySpecCheckpointState state);

/* 항목 */
/* id 길이가 0 이면 Kind 접두어로 다음 번호를 발급한다 (DSN-007). 이미 있는 ID 는 지운 항목의 것이라도 CONFLICT 다. */
KIOTTY_SPEC_API KiottySpecIdResult         KiottySpec_createItem(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecText id, KiottySpecText title, KiottySpecText summary);
/* ID 만 바꾼다. 다른 기록은 ID 가 아니라 내부 키로 항목을 가리키므로 함께 고칠 것이 없다. 체크포인트에 묶이지 않고 지난 시점도 새 ID 로 보인다. */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_renameItem(KiottySpecContext *context, KiottySpecText id, KiottySpecText new_id);
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItem(KiottySpecContext *context, KiottySpecText id);
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItemByHandle(KiottySpecContext *context, KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext *context, KiottySpecText id);
/* data 가 NULL 인 입력은 바꾸지 않는다. */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_updateItem(KiottySpecContext *context, KiottySpecText id, KiottySpecText title, KiottySpecText summary);
/* parent_id 길이가 0 이면 상위 항목을 없앤다. */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_setItemParent(KiottySpecContext *context, KiottySpecText id, KiottySpecText parent_id);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_removeItem(KiottySpecContext *context, KiottySpecText id);

/* 본문 (3.7, C 인터페이스 v2 3.6) */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_writeItemBody(KiottySpecContext *context, KiottySpecText id, KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readItemBody(KiottySpecContext *context, KiottySpecText id, char *buffer, size_t capacity);

/* 태그 (3.6). 0 을 쓰면 레코드를 지운다. 레코드가 없으면 readItemTag 는 value = 0 으로 SUCCESS */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_setItemTag(KiottySpecContext *context, KiottySpecItemHandle item, uint32_t tag, uint64_t value);
KIOTTY_SPEC_API KiottySpecTagResult        KiottySpec_readItemTag(KiottySpecContext *context, KiottySpecItemHandle item, uint32_t tag);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openTagCursor(KiottySpecContext *context, KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchTagPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecTagPage *page);

/* 관계 (3.5) */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_addRelation(KiottySpecContext *context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_removeRelation(KiottySpecContext *context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationCursor(KiottySpecContext *context, KiottySpecText id, KiottySpecDirection direction);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationCursorByModel(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t relation);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchRelationPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecRelationPage *page);

/* 목록 (8.3, 8.5). tag_queries 는 0 ~ KIOTTY_SPEC_MAX_TAG_QUERIES 개이며 AND 다. kind 가 0 이면 모든 Kind */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openItemCursor(KiottySpecContext *context, KiottySpecItemKind kind, const KiottySpecTagQuery *tag_queries, size_t count);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openChildCursor(KiottySpecContext *context, KiottySpecText id);
/* 이 항목을 (깊이 depth 까지) 가리키는 하류 항목. 결과는 ID 페이지다. */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openImpactCursor(KiottySpecContext *context, KiottySpecText id, uint32_t depth);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchItemPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecItemPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchIdPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecIdPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_closeCursor(KiottySpecContext *context, KiottySpecCursor cursor);

/* 체크포인트 (5). 쓰기는 가장 최근 체크포인트에 묶이며, 그것이 DONE 이면 CONFIRMED 다. */
KIOTTY_SPEC_API KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext *context, KiottySpecStep step, KiottySpecText description);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_resumeCheckpoint(KiottySpecContext *context);
/* 확정 조건을 검사하고 DONE 으로 바꾼다. actor 가 USER 가 아니면 PERMISSION, 조건이 어긋나면 PRECONDITION */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_completeCheckpoint(KiottySpecContext *context, KiottySpecActor actor);
/* handle 이 KIOTTY_SPEC_CHECKPOINT_CURRENT 이면 가장 최근 체크포인트 */
KIOTTY_SPEC_API KiottySpecCheckpointResult KiottySpec_readCheckpoint(KiottySpecContext *context, KiottySpecCheckpointHandle handle);
/* step 이 0 이면 모든 단계 */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openCheckpointCursor(KiottySpecContext *context, KiottySpecStep step);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchCheckpointPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecCheckpointPage *page);
/* base 이전의 체크포인트와 거기에만 속한 버전을 지운다. 값은 지운 체크포인트 수 */
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_pruneCheckpoints(KiottySpecContext *context, KiottySpecActor actor, KiottySpecCheckpointHandle base);

/* 결정 기록 (6). 결정 기록이 있는 Kind: 요구사항, 개발 수칙, 외부 인터페이스, 설계 사항, 유닛 테스트, 마일스톤 */
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_writeDecision(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readDecision(KiottySpecContext *context, KiottySpecItemKind kind, char *buffer, size_t capacity);

/* 모델 (3.3 ~ 3.5) */
KIOTTY_SPEC_API KiottySpecModelResult      KiottySpec_readModel(KiottySpecContext *context, KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_readTagDefinition(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t index, KiottySpecTagDefinition *out);
KIOTTY_SPEC_API KiottySpecRelationDefinitionResult KiottySpec_readRelationDefinition(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t index);

/* 위치 */
KIOTTY_SPEC_API KiottySpecPositionResult   KiottySpec_readPosition(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openStepRunCursor(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchStepRunPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecStepRunPage *page);

/* 체크포인트 사이 비교 (5.4). from < to 여야 하며 to 가 KIOTTY_SPEC_CHECKPOINT_CURRENT 이면 가장 최근 체크포인트다.
 * 거꾸로 된 구간은 INVALID_ARGUMENT, 정리로 지워진 체크포인트는 NOT_FOUND */
typedef enum KiottySpecChange {
    KIOTTY_SPEC_CHANGE_ADDED = 1,
    KIOTTY_SPEC_CHANGE_MODIFIED,
    KIOTTY_SPEC_CHANGE_REMOVED
} KiottySpecChange;

typedef enum KiottySpecChangedField {
    KIOTTY_SPEC_CHANGED_FIELD_TITLE   = 1 << 0,
    KIOTTY_SPEC_CHANGED_FIELD_SUMMARY = 1 << 1,
    KIOTTY_SPEC_CHANGED_FIELD_PARENT  = 1 << 2,
    KIOTTY_SPEC_CHANGED_FIELD_TAGS    = 1 << 3,
    KIOTTY_SPEC_CHANGED_FIELD_BODY    = 1 << 4
} KiottySpecChangedField;

typedef struct KiottySpecDiffQuery {
    KiottySpecCheckpointHandle from;
    KiottySpecCheckpointHandle to;
    uint32_t                   kind_mask;       /* 비트 (kind - 1). 0 이면 모든 Kind */
    uint32_t                   field_mask;      /* KiottySpecChangedField. 0 이면 거르지 않음 */
} KiottySpecDiffQuery;                          /* 24 바이트 */

typedef struct KiottySpecItemDiff {
    KiottySpecCheckpointHandle before;          /* from 시점 버전의 체크포인트. 추가면 0 */
    KiottySpecCheckpointHandle after;           /* to 시점 버전의 체크포인트. 삭제면 지운 체크포인트 */
    KiottySpecItemHandle       item;            /* 지금 풀에 있으면 그 핸들, 없으면 INVALID */
    KiottySpecItemKind         kind;
    KiottySpecChange           change;
    uint32_t                   changed_fields;  /* 수정일 때만 */
    uint32_t                   reserved;
    char                       id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
} KiottySpecItemDiff;                           /* 56 바이트 */

typedef struct KiottySpecRelationDiff {
    KiottySpecCheckpointHandle checkpoint;      /* 추가 · 삭제가 일어난 체크포인트 */
    KiottySpecItemKind         model;
    uint32_t                   relation;
    KiottySpecChange           change;          /* ADDED 또는 REMOVED */
    uint32_t                   reserved;
    char                       from_id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char                       to_id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
} KiottySpecRelationDiff;                       /* 56 바이트 */

typedef struct KiottySpecKindDiff {
    uint32_t added;
    uint32_t modified;
    uint32_t removed;
    uint32_t relations_added;
    uint32_t relations_removed;
    uint32_t decision_changed;                  /* 0 또는 1 */
} KiottySpecKindDiff;                           /* 24 바이트 */

typedef struct KiottySpecDiffSummaryResult {
    KiottySpecStatus           code;
    uint32_t                   reserved;
    KiottySpecCheckpointHandle from;
    KiottySpecCheckpointHandle to;              /* CURRENT 를 실제 체크포인트로 바꾼 값 */
    KiottySpecKindDiff         kinds[KIOTTY_SPEC_ITEM_KIND_COUNT];  /* index = kind - 1 */
} KiottySpecDiffSummaryResult;                  /* 336 바이트 */

typedef struct KiottySpecItemDiffPage     { size_t length; KiottySpecItemDiff     elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecItemDiffPage;
typedef struct KiottySpecRelationDiffPage { size_t length; KiottySpecRelationDiff elements[KIOTTY_SPEC_PAGE_CAPACITY]; } KiottySpecRelationDiffPage;

typedef enum KiottySpecTextField {
    KIOTTY_SPEC_TEXT_FIELD_TITLE   = KIOTTY_SPEC_CHANGED_FIELD_TITLE,
    KIOTTY_SPEC_TEXT_FIELD_SUMMARY = KIOTTY_SPEC_CHANGED_FIELD_SUMMARY
} KiottySpecTextField;

typedef enum KiottySpecTextChange {
    KIOTTY_SPEC_TEXT_UNCHANGED = 0,
    KIOTTY_SPEC_TEXT_ADDED,
    KIOTTY_SPEC_TEXT_REMOVED
} KiottySpecTextChange;

/* text 는 커서 풀 안을 가리킨다. 커서를 닫을 때까지만 유효하며 null 종료가 아니다 */
typedef struct KiottySpecTextDiff {
    const char          *text;
    size_t               length;                /* 0 이면 더 읽을 조각이 없다 */
    KiottySpecTextChange change;
    uint32_t             reserved;
} KiottySpecTextDiff;                           /* 24 바이트 */

typedef struct KiottySpecTextDiffResult { KiottySpecStatus code; uint32_t reserved; KiottySpecTextDiff value; } KiottySpecTextDiffResult;

KIOTTY_SPEC_API KiottySpecDiffSummaryResult KiottySpec_summarizeDiff(KiottySpecContext *context, KiottySpecDiffQuery query);
/* Kind 오름차순, 그다음 ID 오름차순 */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openItemDiffCursor(KiottySpecContext *context, KiottySpecDiffQuery query);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchItemDiffPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecItemDiffPage *page);
/* Kind, from_id, relation, to_id 순. field_mask 는 쓰지 않는다 */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationDiffCursor(KiottySpecContext *context, KiottySpecDiffQuery query);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchRelationDiffPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecRelationDiffPage *page);
/* 그 시점의 버전. 지워진 항목도 그 시점에 있었으면 읽는다. 그 시점에 없거나 지워져 있으면 NOT_FOUND.
 * handle 은 지금 풀에 있으면 그 핸들, 없으면 INVALID. parent 는 지금 풀에 있는 상위 항목이면 그 핸들 */
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItemAt(KiottySpecContext *context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint);
KIOTTY_SPEC_API KiottySpecTagResult        KiottySpec_readItemTagAt(KiottySpecContext *context, KiottySpecText id, uint32_t tag, KiottySpecCheckpointHandle checkpoint);
/* 본문은 현재 것만 파일에 있다. 그 시점의 본문이 지금과 같을 때만 읽고, 다르면 NOT_FOUND (과거 본문은 git 이력에 있다) */
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readItemBodyAt(KiottySpecContext *context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint, char *buffer, size_t capacity);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readDecisionAt(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecCheckpointHandle checkpoint, char *buffer, size_t capacity);
/* 제목 · 요약의 글자 diff. total 은 조각 수. 본문(BODY)이나 비트 둘 이상은 INVALID_ARGUMENT */
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openTextDiffCursor(KiottySpecContext *context, KiottySpecItemHandle item, KiottySpecTextField field,
                                                                         KiottySpecCheckpointHandle from, KiottySpecCheckpointHandle to);
KIOTTY_SPEC_API KiottySpecTextDiffResult   KiottySpec_nextTextDiff(KiottySpecContext *context, KiottySpecCursor cursor);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_closeTextDiffCursor(KiottySpecContext *context, KiottySpecCursor cursor);

#ifdef __cplusplus
}
#endif

#endif
