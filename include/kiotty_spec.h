/*
 * kiotty_spec.h — Kiotty-Spec 로컬 사양 DB 의 C 인터페이스
 *
 * 명명 규칙
 *  - 함수      : KiottySpec_<그룹>_<동사>  예) KiottySpec_Item_create, KiottySpec_Checkpoint_confirm
 *                DB 자체를 다루는 함수만 그룹 없이 KiottySpec_open 처럼 쓴다.
 *  - 타입      : KiottySpec<이름>          예) KiottySpecItem, KiottySpecStatus
 *  - 열거 상수 : <타입 이름>_<값>           예) KiottySpecItemKind_Requirement
 *
 * 목록 접근 규칙
 *  - 목록이나 필터된 뷰를 돌려주는 함수는 이름이 ...Begin 으로 끝나고 KiottySpecCursor 를 돌려준다.
 *  - KiottySpec_Cursor_count 로 개수를, KiottySpec_Cursor_get<타입>(cursor, index, &out) 으로 원소를 읽는다.
 *    get 이 돌려주는 포인터는 커서가 소유하며 KiottySpec_Cursor_end 전까지만 유효하다.
 *  - KiottySpec_Cursor_end 가 커서와 모든 원소 메모리를 정리한다.
 *
 * 그 밖의 규약
 *  - 모든 함수는 KiottySpecStatus 를 돌려주고 결과는 out 파라미터로 준다.
 *  - 문자열은 UTF-8, NUL 종료. 단일 레코드나 문자열을 돌려받은 경우 KiottySpec_free 로 해제한다.
 *  - ID 는 사람이 읽는 문자열이다. 예) "REQ-003", "DSN-021", "CP-0042", "CR-007", "FILE-015"
 *  - 쓰기 함수는 actor(User / Ai) 를 받는다. 체크포인트 확정은 User 만 가능하다.
 *  - 모든 기록은 (체크포인트, 종류, ID) 로 식별된다. 쓰기는 항상 "현재(미확정) 체크포인트"에 들어간다.
 *  - CLI 는 이 헤더와 1:1 로 대응한다. 예) `ks item get REQ-003` == KiottySpec_Item_get
 *  - 용어와 엔티티는 definition.md 를 따른다.
 */
#ifndef KIOTTY_SPEC_H
#define KIOTTY_SPEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KIOTTYSPEC_API
#define KIOTTYSPEC_API
#endif

/* ========================================================================
 * 1. 불투명 핸들
 * ====================================================================== */

typedef struct KiottySpec       KiottySpec;        /* 열린 프로젝트 DB */
typedef struct KiottySpecCursor KiottySpecCursor;  /* 목록 커서. ...Begin 이 만들고 Cursor_end 가 정리한다 */

/* ========================================================================
 * 2. 열거형
 * ====================================================================== */

typedef enum KiottySpecStatus {
    KiottySpecStatus_Ok = 0,                 /* 성공 */
    KiottySpecStatus_NotFound,               /* ID 에 해당하는 기록 없음 */
    KiottySpecStatus_InvalidArg,             /* 인자 오류. 허용되지 않는 링크 쌍, 결정 기록 길이 초과 포함 (R-7, R-11) */
    KiottySpecStatus_Confirmed,              /* 확정된 체크포인트의 내용을 바꾸려 함 (R-1) */
    KiottySpecStatus_Permission,             /* 액터 권한 부족. 예) Ai 가 체크포인트 확정 (R-2) */
    KiottySpecStatus_Precondition,           /* 확정 조건·RELEASE 조건·1:1·포함 조건 미충족 (R-3, R-4, R-5, R-10) */
    KiottySpecStatus_Conflict,               /* jsonl 가져오기 등에서 같은 ID 충돌 */
    KiottySpecStatus_WrongCursorType,        /* 커서 원소 종류와 다른 get 함수를 호출함 */
    KiottySpecStatus_Io,                     /* 파일 입출력 오류 */
    KiottySpecStatus_Db                      /* SQLite 내부 오류 */
} KiottySpecStatus;

typedef enum KiottySpecActor {
    KiottySpecActor_User = 1,                /* 사용자 */
    KiottySpecActor_Ai                       /* AI */
} KiottySpecActor;

typedef enum KiottySpecItemKind {
    KiottySpecItemKind_Requirement = 1,      /* 요구사항 (REQ) */
    KiottySpecItemKind_Rule,                 /* 개발 규칙 (RULE) */
    KiottySpecItemKind_Interface,            /* 외부 인터페이스 항목 (UI) */
    KiottySpecItemKind_Design,               /* 설계 항목 (DSN) */
    KiottySpecItemKind_UnitTest,             /* 유닛 테스트 (UT) */
    KiottySpecItemKind_SystemTest,           /* 시스템 테스트 (ST) */
    KiottySpecItemKind_E2eTest,              /* E2E 테스트 (E2E) */
    KiottySpecItemKind_Milestone             /* 마일스톤 (MS) */
} KiottySpecItemKind;

typedef enum KiottySpecItemCategory {
    KiottySpecItemCategory_None = 0,         /* 분류 없음 (테스트, 마일스톤) */
    KiottySpecItemCategory_ReqFunctional,    /* 요구사항: 기능 */
    KiottySpecItemCategory_ReqQuality,       /* 요구사항: 품질 */
    KiottySpecItemCategory_ReqEnvironment,   /* 요구사항: 환경 */
    KiottySpecItemCategory_RuleArchitecture, /* 개발 규칙: 아키텍처 */
    KiottySpecItemCategory_RuleConvention,   /* 개발 규칙: 컨벤션 */
    KiottySpecItemCategory_RuleCoding,       /* 개발 규칙: 코딩 제약 */
    KiottySpecItemCategory_RuleDependency,   /* 개발 규칙: 의존성 */
    KiottySpecItemCategory_RuleTest,         /* 개발 규칙: 테스트 */
    KiottySpecItemCategory_UiView,           /* 외부 인터페이스: GUI 뷰 */
    KiottySpecItemCategory_UiCApi,           /* 외부 인터페이스: C 함수·타입 */
    KiottySpecItemCategory_DsnModule,        /* 설계: 모듈 */
    KiottySpecItemCategory_DsnFlow,          /* 설계: 동작 흐름 */
    KiottySpecItemCategory_DsnDirectory,     /* 설계: 디렉토리 */
    KiottySpecItemCategory_DsnMemory         /* 설계: 메모리 정책 */
} KiottySpecItemCategory;

typedef enum KiottySpecFileRole {
    KiottySpecFileRole_GuiDraft = 1,         /* GUI 초안 html */
    KiottySpecFileRole_CHeader,              /* C/C++ 헤더 */
    KiottySpecFileRole_Source                /* 소스 코드 */
} KiottySpecFileRole;

typedef enum KiottySpecLinkType {
    KiottySpecLinkType_Satisfies = 1,        /* 만족시킨다: 인터페이스 → 요구사항, 설계 → 요구사항/인터페이스 */
    KiottySpecLinkType_Verifies,             /* 검증한다: UT → 설계, ST → 기능 요구사항, E2E → 인터페이스 */
    KiottySpecLinkType_Implements,           /* 구현한다: 인터페이스 파일 → 인터페이스, 소스 파일 → 설계 */
    KiottySpecLinkType_Covers,               /* 포함한다: 마일스톤 → 설계/유닛 테스트 */
    KiottySpecLinkType_DependsOn             /* 의존한다: 모듈 → 모듈 */
} KiottySpecLinkType;

typedef enum KiottySpecDirection {
    KiottySpecDirection_Upstream = 1,        /* 요구사항 쪽으로 (링크의 to 방향) */
    KiottySpecDirection_Downstream,          /* 코드 쪽으로 (링크의 from 방향) */
    KiottySpecDirection_Both                 /* 양쪽 */
} KiottySpecDirection;

typedef enum KiottySpecChangeSource {
    KiottySpecChangeSource_Feedback = 1,     /* 사용자 피드백 */
    KiottySpecChangeSource_Issue             /* 이슈 (RELEASE 이후) */
} KiottySpecChangeSource;

typedef enum KiottySpecChangeState {
    KiottySpecChangeState_Open = 1,          /* 열림 */
    KiottySpecChangeState_InProgress,        /* 반영 중 */
    KiottySpecChangeState_Done,              /* 완료 */
    KiottySpecChangeState_Rejected           /* 거부 */
} KiottySpecChangeState;

typedef enum KiottySpecImplState {
    KiottySpecImplState_NotImplemented = 1,  /* 미구현 */
    KiottySpecImplState_NeedsChange,         /* 변경 필요 (구현 완료 뒤 설계가 바뀜, R-6) */
    KiottySpecImplState_Implemented          /* 구현 완료 */
} KiottySpecImplState;

typedef enum KiottySpecTestResult {
    KiottySpecTestResult_Pass = 1,           /* 통과 */
    KiottySpecTestResult_Fail                /* 실패 (코드 검사면 위반 있음) */
} KiottySpecTestResult;

typedef enum KiottySpecScope {
    KiottySpecScope_IntraModule = 1,         /* 모듈 내: 다른 모듈에 닿지 않음 → Step 7 에서 바로 수정 */
    KiottySpecScope_InterModule              /* 모듈 간: 다른 모듈에 닿음 → 변경 요청 필요 */
} KiottySpecScope;

typedef enum KiottySpecDecisionKind {
    KiottySpecDecisionKind_Requirement = 1,  /* 결정 기록: 요구사항 */
    KiottySpecDecisionKind_Rule,             /* 결정 기록: 개발 규칙 */
    KiottySpecDecisionKind_Interface,        /* 결정 기록: 외부 인터페이스 */
    KiottySpecDecisionKind_Design,           /* 결정 기록: 설계 */
    KiottySpecDecisionKind_UnitTest,         /* 결정 기록: 유닛 테스트 */
    KiottySpecDecisionKind_Plan              /* 결정 기록: 개발 계획 */
} KiottySpecDecisionKind;

typedef enum KiottySpecCheckpointPurpose {
    KiottySpecCheckpointPurpose_None = 0,    /* 일반 라운드 */
    KiottySpecCheckpointPurpose_Release      /* 최초 개발 완료 (RELEASE) */
} KiottySpecCheckpointPurpose;

/* ========================================================================
 * 3. 레코드 구조체 (읽기 결과)
 * ====================================================================== */

typedef struct KiottySpecCheckpoint {
    const char *id;                          /* "CP-0042" */
    int32_t step;                            /* 소속 개발 단계 1 ~ 10 */
    bool confirmed;                          /* 사용자 확정 여부 */
    KiottySpecCheckpointPurpose purpose;     /* 일반 / RELEASE */
    const char *note;                        /* 라운드 요약 한 줄 */
    int64_t createdAt;                       /* unix epoch */
} KiottySpecCheckpoint;

typedef struct KiottySpecItem {
    const char *id;                          /* "REQ-003" */
    KiottySpecItemKind kind;                 /* 종류 */
    KiottySpecItemCategory category;         /* 분류 */
    const char *parentId;                    /* 상위 항목 (없으면 NULL) */
    int32_t seq;                             /* 형제 간 순서 */
    const char *title;                       /* 제목 */
    const char *body;                        /* 본문 (Markdown) */
    const char *cpFrom;                      /* 이 버전이 만들어진 체크포인트 */
} KiottySpecItem;

typedef struct KiottySpecMilestoneEntry {
    const char *milestoneId;                 /* 마일스톤 */
    const char *itemId;                      /* 포함된 설계 항목 또는 유닛 테스트 */
    KiottySpecImplState state;               /* 미구현 / 변경 필요 / 구현 완료 (계산 값) */
    const char *implementedCp;               /* 구현 완료로 기록된 체크포인트 (없으면 NULL) */
} KiottySpecMilestoneEntry;

typedef struct KiottySpecFile {
    const char *id;                          /* "FILE-015" */
    KiottySpecFileRole role;                 /* GUI 초안 / 헤더 / 소스 */
    const char *path;                        /* 프로젝트 루트 기준 경로 */
    const char *hash;                        /* 마지막 기록 해시 */
    const char *cpFrom;                      /* 이 해시가 기록된 체크포인트 */
} KiottySpecFile;

typedef struct KiottySpecLink {
    const char *fromId;                      /* 주어 */
    const char *toId;                        /* 목적어 */
    KiottySpecLinkType type;                 /* 관계 */
    const char *cpFrom;                      /* 만들어진 체크포인트 */
} KiottySpecLink;

typedef struct KiottySpecChangeRequest {
    const char *id;                          /* "CR-007" */
    KiottySpecChangeSource source;           /* 피드백 / 이슈 */
    const char *body;                        /* 요구 내용 */
    int32_t entryStep;                       /* 진입 단계 */
    KiottySpecChangeState state;             /* 상태 */
    const char *cpOpened;                    /* 접수 체크포인트 */
    const char *cpClosed;                    /* 완료 확정 체크포인트 (없으면 NULL) */
} KiottySpecChangeRequest;

typedef struct KiottySpecResult {
    const char *targetId;                    /* 테스트 ID 또는 (코드 검사) 개발 규칙 ID */
    KiottySpecTestResult result;             /* PASS / FAIL */
    const char *log;                         /* 출력. 검사 FAIL 이면 위반 위치 목록 */
    const char *cpFrom;                      /* 실행한 체크포인트 */
    int64_t runAt;                           /* unix epoch */
} KiottySpecResult;

typedef struct KiottySpecPosition {
    const char *checkpointId;                /* 가장 최근 체크포인트 (없으면 NULL) */
    int32_t step;                            /* 그 체크포인트의 단계 (없으면 0) */
    bool confirmed;                          /* 그 체크포인트가 확정되었는지 */
    bool released;                           /* RELEASE 이후인지 (유지보수 단계) */
    size_t pendingWrites;                    /* 아직 체크포인트에 묶이지 않은 쓰기 수 */
    int32_t nextStep;                        /* 다음에 진행할 단계 */
    const char *nextAction;                  /* 다음에 할 일 한 줄 */
} KiottySpecPosition;

typedef struct KiottySpecStepRun {
    int32_t step;                            /* 연속으로 머문 단계 */
    const char *firstCheckpointId;           /* 이 구간의 첫 체크포인트 */
    const char *lastCheckpointId;            /* 이 구간의 마지막 체크포인트 */
    const char *confirmedCheckpointId;       /* 이 구간에서 확정된 마지막 체크포인트 (없으면 NULL) */
    size_t checkpointCount;                  /* 이 구간의 라운드 수 */
    const char *lastNote;                    /* 마지막 라운드 요약 */
    const char *reason;                      /* 이 단계로 온 이유: "시작" / "다음 단계" / "CR-007 로 되돌아옴" / "RELEASE" */
} KiottySpecStepRun;

typedef struct KiottySpecItemFilter {
    KiottySpecItemKind kind;                 /* 0 이면 전체 */
    KiottySpecItemCategory category;         /* 0 이면 전체 */
    const char *parentId;                    /* NULL 이면 무시 */
    const char *text;                        /* NULL 이면 무시. 제목·본문 검색 */
    const char *atCheckpoint;                /* NULL 이면 현재, 아니면 그 체크포인트 시점의 뷰 */
} KiottySpecItemFilter;

/* ========================================================================
 * 4. DB 수명 주기, 트랜잭션, 메모리
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_open(const char *projectRoot, KiottySpec **out);                 /* 프로젝트 루트의 .kiotty/spec.db 를 연다 (없으면 생성) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_close(KiottySpec *db);                                           /* DB 를 닫는다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_init(KiottySpec *db, const char *projectName, bool installGitHooks); /* 프로젝트 메타데이터를 만들고 선택적으로 pre-commit/post-merge 훅을 설치한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_setPolicy(KiottySpec *db, const char *key, const char *value);   /* 정책 설정. 예) decision_max_line=200, decision_max_concerns=8 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_getPolicy(KiottySpec *db, const char *key, char **outValue);     /* 정책 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Transaction_start(KiottySpec *db);                               /* 여러 쓰기를 원자적으로 묶기 시작 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Transaction_commit(KiottySpec *db);                              /* 트랜잭션 커밋 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Transaction_rollback(KiottySpec *db);                            /* 트랜잭션 롤백 */
KIOTTYSPEC_API void             KiottySpec_free(void *ptr);                                                 /* 단일 레코드·문자열 해제 */
KIOTTYSPEC_API const char      *KiottySpec_strerror(KiottySpecStatus status);                               /* 상태 코드 설명 */
KIOTTYSPEC_API const char      *KiottySpec_enumName(const char *enumType, int value);                       /* 열거값의 이름. enumType 예) "ItemKind" → 1 은 "requirement". 모르면 "unknown" */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_enumParse(const char *enumType, const char *name, int *outValue); /* 이름을 열거값으로. enumType: ItemKind, ItemCategory, FileRole, LinkType, Direction, ChangeSource, ChangeState, ImplState, TestResult, Scope, DecisionKind, CheckpointPurpose, Actor */
KIOTTYSPEC_API const char      *KiottySpec_lastError(void);                                                 /* 이 스레드에서 마지막으로 실패한 호출의 상세 메시지 (없으면 빈 문자열) */

/* ========================================================================
 * 5. 커서 — 모든 ...Begin 의 결과를 읽는 공통 방법
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_count(KiottySpecCursor *cursor, size_t *outCount);                                        /* 원소 개수 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getItem(KiottySpecCursor *cursor, size_t index, const KiottySpecItem **out);               /* index 번째 항목 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getLink(KiottySpecCursor *cursor, size_t index, const KiottySpecLink **out);               /* index 번째 링크 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getFile(KiottySpecCursor *cursor, size_t index, const KiottySpecFile **out);               /* index 번째 파일 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getCheckpoint(KiottySpecCursor *cursor, size_t index, const KiottySpecCheckpoint **out);   /* index 번째 체크포인트 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getChangeRequest(KiottySpecCursor *cursor, size_t index, const KiottySpecChangeRequest **out); /* index 번째 변경 요청 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getResult(KiottySpecCursor *cursor, size_t index, const KiottySpecResult **out);           /* index 번째 결과 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getMilestoneEntry(KiottySpecCursor *cursor, size_t index, const KiottySpecMilestoneEntry **out); /* index 번째 마일스톤 포함 항목 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getStepRun(KiottySpecCursor *cursor, size_t index, const KiottySpecStepRun **out);         /* index 번째 단계 구간 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Cursor_getId(KiottySpecCursor *cursor, size_t index, const char **out);                          /* index 번째 원소의 ID. 모든 커서에서 동작한다 (링크는 fromId, 결과는 targetId, 마일스톤 포함 항목은 itemId) */
KIOTTYSPEC_API void             KiottySpec_Cursor_end(KiottySpecCursor *cursor);                                                            /* 커서와 원소 메모리를 모두 정리한다 */

/* ========================================================================
 * 6. 체크포인트 — 라운드마다 하나, 사용자가 확정
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_create(KiottySpec *db, KiottySpecActor actor, int32_t step, const char *note, char **outId); /* 라운드를 마치고 현재 작업 내용을 새 체크포인트로 찍는다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_confirm(KiottySpec *db, KiottySpecActor actor, const char *cpId);                     /* 체크포인트를 확정한다. User 만 가능, 단계별 확정 조건 미충족 시 Precondition (R-2 ~ R-5) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_get(KiottySpec *db, const char *cpId, KiottySpecCheckpoint **out);                    /* 체크포인트 하나 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_current(KiottySpec *db, KiottySpecCheckpoint **out);                                  /* 가장 최근 체크포인트 (미확정일 수 있음) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_lastConfirmed(KiottySpec *db, int32_t stepOr0, KiottySpecCheckpoint **out);           /* 마지막 확정 체크포인트. step 을 주면 그 단계의 것 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_listBegin(KiottySpec *db, int32_t stepOr0, KiottySpecCursor **outCursor);              /* 체크포인트 목록 (getCheckpoint) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_changedItemsBegin(KiottySpec *db, const char *cpFrom, const char *cpToOrNull, KiottySpecCursor **outCursor); /* 두 체크포인트 사이에 바뀐 항목 (getItem). cpTo 가 NULL 이면 현재까지 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_diff(KiottySpec *db, const char *cpA, const char *cpB, char **outMarkdown);           /* 두 체크포인트 간 추가·수정·삭제를 Markdown 으로 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_confirmCheck(KiottySpec *db, int32_t step, bool *outOk, char **outReport);            /* 이 단계를 지금 확정할 수 있는지와 그 이유 (R-3 ~ R-5) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_releaseCheck(KiottySpec *db, bool *outOk, char **outReport);                          /* 지금 RELEASE 할 수 있는지와 그 이유 (R-10) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_release(KiottySpec *db, KiottySpecActor actor, const char *note, char **outId);        /* RELEASE 체크포인트를 만들고 확정한다. 조건 미충족 시 Precondition (R-10) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_position(KiottySpec *db, KiottySpecPosition **out);                                   /* 지금 어느 체크포인트·단계에 있고 다음에 무엇을 할지 (오랜만에 돌아왔을 때) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_journeyBegin(KiottySpec *db, KiottySpecCursor **outCursor);                           /* 지금까지 밟아온 단계를 연속 구간으로 묶은 이력 (getStepRun) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Checkpoint_prune(KiottySpec *db, KiottySpecActor actor, const char *baseCpId, size_t *outRemoved); /* 기준 체크포인트 이전 기록을 모두 지운다 (Compaction). 최신 확정·RELEASE 는 기준으로만 가능 */

/* ========================================================================
 * 7. 항목 — 요구사항·규칙·인터페이스·설계·테스트·마일스톤 공통
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_create(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, KiottySpecItemCategory category,
                                                        const char *parentIdOrNull, const char *title, const char *body, char **outId);     /* 항목을 만들고 ID 를 발급한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_get(KiottySpec *db, const char *itemId, KiottySpecItem **out);                               /* 현재 버전 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_getAt(KiottySpec *db, const char *itemId, const char *cpId, KiottySpecItem **out);           /* 특정 체크포인트 시점의 버전 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_update(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *title, const char *body); /* 제목·본문을 바꾼다. 새 버전은 현재 체크포인트에 속한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_setParent(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *parentIdOrNull, int32_t seq); /* 상위 항목과 순서를 바꾼다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_remove(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *reason);       /* 현재 체크포인트에서 항목을 제거한다. 이전 체크포인트에는 남는다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_exists(KiottySpec *db, const char *itemId, bool *outExists);                                 /* 현재 뷰에 존재하는지 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_listBegin(KiottySpec *db, const KiottySpecItemFilter *filter, KiottySpecCursor **outCursor); /* 필터로 항목 열거 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_childrenBegin(KiottySpec *db, const char *itemId, bool recursive, KiottySpecCursor **outCursor); /* 하위 항목 열거 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_searchBegin(KiottySpec *db, const char *query, size_t limit, KiottySpecCursor **outCursor);  /* 제목·본문 전문 검색 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Item_template(KiottySpecItemKind kind, KiottySpecItemCategory category, char **outMarkdown);     /* 종류별 본문 틀(definition.md 2.2.1)을 돌려준다 */

/* ========================================================================
 * 8. 마일스톤 — 포함 항목과 구현 상태
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Milestone_setEntryState(KiottySpec *db, KiottySpecActor actor, const char *milestoneId, const char *itemId, KiottySpecImplState state); /* 포함 항목의 상태를 기록한다. 구현 완료면 현재 체크포인트가 implementedCp 가 된다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Milestone_entriesBegin(KiottySpec *db, const char *milestoneId, KiottySpecCursor **outCursor);     /* 포함 항목과 계산된 상태 (getMilestoneEntry, R-6) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Milestone_uncoveredBegin(KiottySpec *db, KiottySpecCursor **outCursor);                            /* 어떤 마일스톤에도 포함되지 않은 설계 항목·유닛 테스트 (getItem, R-5) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Milestone_next(KiottySpec *db, char **outMilestoneId);                                              /* 미구현·변경 필요 항목이 남은 첫 마일스톤 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Milestone_progress(KiottySpec *db, size_t *outTotal, size_t *outImplemented, size_t *outNeedsChange); /* 전체 포함 항목 수와 상태별 집계 */

/* ========================================================================
 * 9. 파일 — GUI 초안, 헤더, 소스
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_register(KiottySpec *db, KiottySpecActor actor, KiottySpecFileRole role, const char *path, char **outId); /* 파일을 등록하고 해시를 기록한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_get(KiottySpec *db, const char *fileId, KiottySpecFile **out);                                 /* 파일 하나 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_byPath(KiottySpec *db, const char *path, KiottySpecFile **out);                                /* 경로로 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_rescan(KiottySpec *db, KiottySpecActor actor, const char *fileId, bool *outChanged);           /* 해시를 다시 계산해 기록하고 바뀌었는지 알려준다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_listBegin(KiottySpec *db, KiottySpecFileRole roleOr0, KiottySpecCursor **outCursor);          /* 역할별 파일 열거 (getFile) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_changedSinceBegin(KiottySpec *db, const char *cpId, KiottySpecCursor **outCursor);           /* 체크포인트 이후 해시가 바뀐 파일 (getFile) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_File_scanGuiDraft(KiottySpec *db, const char *fileId, KiottySpecCursor **outUnknownIds, KiottySpecCursor **outMissingViews); /* html 의 data-ks-id 를 읽어 DB 에 없는 ID(getId) 와 html 에 없는 뷰(getItem) 를 보고 */

/* ========================================================================
 * 10. 링크 — 추적과 영향
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_add(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type); /* "from 은 to 를 ~한다" 링크. 허용되지 않는 쌍이면 InvalidArg (R-11) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_remove(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type); /* 링크 제거 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_listBegin(KiottySpec *db, const char *id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, KiottySpecCursor **outCursor); /* 항목·파일에 걸린 링크 (getLink) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_upstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor);  /* 요구사항 방향으로 깊이 제한 내 연결된 항목·파일 ID (getId). depth 0 은 무제한. "왜 이 코드가 있는가" */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_downstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor); /* 코드 방향으로 연결된 항목·파일 ID (getId). "어디에 구현되고 무엇으로 검증되는가" */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_impactBegin(KiottySpec *db, const char *sinceCpId, KiottySpecCursor **outCursor);            /* 체크포인트 이후 바뀐 항목과 링크된 영향 항목 (getItem). 각 단계의 "다시 볼 것" */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_scopeOf(KiottySpec *db, const char *designOrFileId, KiottySpecScope *outScope, KiottySpecCursor **outAffectedModules); /* 수정이 의존한다 링크로 다른 모듈에 닿는지 판정하고 닿는 모듈을 돌려준다 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Link_matrix(KiottySpec *db, KiottySpecItemKind rowKind, KiottySpecItemKind colKind, char **outCsv); /* 두 종류 간 추적 매트릭스 CSV */

/* ========================================================================
 * 11. 변경 요청 — 피드백과 이슈
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_create(KiottySpec *db, KiottySpecActor actor, KiottySpecChangeSource source, const char *body, int32_t entryStep, char **outId); /* 변경 요청을 열고 진입 단계를 기록한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_get(KiottySpec *db, const char *crId, KiottySpecChangeRequest **out);                /* 변경 요청 하나 조회 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_setState(KiottySpec *db, KiottySpecActor actor, const char *crId, KiottySpecChangeState state, const char *note); /* 상태 변경. Done 은 현재 체크포인트를 cpClosed 로 기록 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_setEntryStep(KiottySpec *db, KiottySpecActor actor, const char *crId, int32_t entryStep); /* 영향 분석 뒤 진입 단계를 정정한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_listBegin(KiottySpec *db, KiottySpecChangeState stateOr0, KiottySpecCursor **outCursor); /* 상태별 변경 요청 (getChangeRequest) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_ChangeRequest_openCount(KiottySpec *db, size_t *outCount);                                         /* 열린 변경 요청 수 (RELEASE 조건) */

/* ========================================================================
 * 12. 결정 기록 — 종류당 하나, 매 라운드 다시 씀
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Decision_get(KiottySpec *db, KiottySpecDecisionKind kind, char **outMarkdown);                     /* 현재 결정 기록 본문 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Decision_rewrite(KiottySpec *db, KiottySpecActor actor, KiottySpecDecisionKind kind, const char *markdown); /* 결정 기록을 통째로 다시 쓴다. 줄 길이·관심사 수 초과면 InvalidArg (R-7) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Decision_check(KiottySpec *db, KiottySpecDecisionKind kind, char **outReport);                      /* 사라진 항목을 가리키는 줄, 길이 초과 줄을 보고 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Decision_getAt(KiottySpec *db, KiottySpecDecisionKind kind, const char *cpId, char **outMarkdown);  /* 특정 체크포인트 시점의 결정 기록 */

/* ========================================================================
 * 13. 결과 — 테스트 실행과 코드 검사
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_record(KiottySpec *db, KiottySpecActor actor, const char *targetId, KiottySpecTestResult result, const char *log); /* 테스트 또는 (대상이 개발 규칙이면) 코드 검사 결과를 기록한다 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_last(KiottySpec *db, const char *targetId, KiottySpecResult **out);                          /* 대상의 마지막 결과 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_listBegin(KiottySpec *db, const char *targetId, KiottySpecCursor **outCursor);               /* 대상의 결과 이력 (getResult) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_failedBegin(KiottySpec *db, KiottySpecItemKind kindOr0, KiottySpecCursor **outCursor);       /* 마지막 결과가 FAIL 인 테스트 또는 개발 규칙 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_summary(KiottySpec *db, KiottySpecItemKind kind, size_t *outTotal, size_t *outPass, size_t *outFail, size_t *outNotRun); /* 종류별 통과·실패·미실행 집계 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Result_suspectsBegin(KiottySpec *db, const char *failedTargetId, KiottySpecCursor **outCursor);     /* 실패한 테스트에서 검증한다 → 구현한다 링크를 따라 원인 후보 설계 항목·소스 파일 (getId) */

/* ========================================================================
 * 14. 검사 — 불일치 검출
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_requirementsWithoutCriteriaBegin(KiottySpec *db, KiottySpecCursor **outCursor);              /* 인수 조건 절이 없는 기능 요구사항 (getItem, R-4) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_systemTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated); /* 시스템 테스트가 없거나 둘 이상인 기능 요구사항 (getItem, R-3) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_e2eTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated);    /* E2E 테스트가 없거나 둘 이상인 외부 인터페이스 항목 (getItem, R-3) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_unlinkedBegin(KiottySpec *db, KiottySpecItemKind kind, KiottySpecCursor **outCursor);        /* 링크가 하나도 없는 항목 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_orphanTestsBegin(KiottySpec *db, KiottySpecCursor **outCursor);                              /* 검증 대상이 사라진 테스트 (getItem) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Check_all(KiottySpec *db, char **outReport);                                                       /* 위 검사와 마일스톤 포함, 결정 기록 검사를 모두 돌려 Markdown 보고서로 */

/* ========================================================================
 * 15. 맥락 — 단계별 입력을 토큰 한도 안에서 묶어 주기
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_forStep(KiottySpec *db, int32_t step, size_t maxBytes, char **outMarkdown);                /* definition.md 4 절 표의 "Kiotty-Spec 입력" 을 그 단계에 맞게 묶는다. 영향 항목이 있으면 그것만 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_forItem(KiottySpec *db, const char *itemId, int32_t depth, size_t maxBytes, char **outMarkdown); /* 항목과 상·하류 연결 항목, 관련 결정 줄 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_forMilestone(KiottySpec *db, const char *milestoneId, size_t maxBytes, char **outMarkdown);    /* 마일스톤 구현에 필요한 설계·유닛 테스트·규칙·결정 줄·소스 파일 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_forChangeRequest(KiottySpec *db, const char *crId, size_t maxBytes, char **outMarkdown);      /* 변경 요청의 영향 항목과 진입 단계 판단 근거 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_rules(KiottySpec *db, char **outMarkdown);                                                 /* 코드 작성 전 항상 읽을 개발 규칙만 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_pending(KiottySpec *db, char **outMarkdown);                                               /* 열린 변경 요청, 실패 결과, 변경 필요 항목, 미확정 체크포인트 등 지금 할 일 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_whereAmI(KiottySpec *db, size_t maxBytes, char **outMarkdown);                          /* 현재 위치, 지나온 단계, 다음 할 일, 마지막 라운드에서 바뀐 것을 한 페이지로 (재개용) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Context_status(KiottySpec *db, char **outMarkdown);                                                /* 현재 단계, 마지막 확정 체크포인트, 마일스톤 진행률, 테스트 집계를 한 화면으로 */

/* ========================================================================
 * 16. 내보내기·가져오기·원시 질의
 * ====================================================================== */

KIOTTYSPEC_API KiottySpecStatus KiottySpec_Export_markdown(KiottySpec *db, KiottySpecItemKind kind, char **outMarkdown);                     /* 종류별 문서(요구사항서, 설계서, 테스트 계획서, 개발 계획서) 생성 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Export_traceReport(KiottySpec *db, char **outMarkdown);                                            /* 전체 추적 매트릭스와 불일치 보고서 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Export_jsonl(KiottySpec *db, const char *dir);                                                     /* DB 를 테이블별 jsonl(ID 정렬)로 내보낸다. pre-commit 훅 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Import_jsonl(KiottySpec *db, const char *dir, bool merge, char **outConflictReport);               /* jsonl 로 DB 를 재생성하거나 병합한다. 같은 ID 충돌은 뒤 체크포인트를 택하고 보고. post-merge 훅 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Import_markdown(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, const char *markdown, KiottySpecCursor **outCreatedIds); /* 정해진 틀의 Markdown 을 항목으로 가져온다 (getId) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Query_readonly(KiottySpec *db, const char *sql, char **outJson);                                   /* 읽기 전용 SQL (예외적 질의용) */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Schema_version(KiottySpec *db, int32_t *outVersion);                                               /* 스키마 버전 */
KIOTTYSPEC_API KiottySpecStatus KiottySpec_Schema_migrate(KiottySpec *db);                                                                    /* 스키마를 현재 라이브러리 버전으로 올린다 */

#ifdef __cplusplus
}
#endif

#endif /* KIOTTY_SPEC_H */
