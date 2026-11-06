#ifndef KIOTTY_SPEC_H
#define KIOTTY_SPEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KIOTTY_SPEC_API
#define KIOTTY_SPEC_API
#endif

typedef struct KiottySpec       KiottySpec;
typedef struct KiottySpecCursor KiottySpecCursor;

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

typedef enum KiottySpecItemKind {
    KIOTTY_SPEC_ITEM_KIND_REQUIREMENT = 1,
    KIOTTY_SPEC_ITEM_KIND_RULE,
    KIOTTY_SPEC_ITEM_KIND_INTERFACE,
    KIOTTY_SPEC_ITEM_KIND_DESIGN,
    KIOTTY_SPEC_ITEM_KIND_UNIT_TEST,
    KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
    KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST,
    KIOTTY_SPEC_ITEM_KIND_MILESTONE
} KiottySpecItemKind;

typedef enum KiottySpecItemCategory {
    KIOTTY_SPEC_ITEM_CATEGORY_NONE = 0,
    KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL,
    KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_QUALITY,
    KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_ENVIRONMENT,
    KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE,
    KIOTTY_SPEC_ITEM_CATEGORY_RULE_CONVENTION,
    KIOTTY_SPEC_ITEM_CATEGORY_RULE_CODING,
    KIOTTY_SPEC_ITEM_CATEGORY_RULE_DEPENDENCY,
    KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST,
    KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_VIEW,
    KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API,
    KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE,
    KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_FLOW,
    KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_DIRECTORY,
    KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MEMORY
} KiottySpecItemCategory;

typedef enum KiottySpecFileRole {
    KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT = 1,
    KIOTTY_SPEC_FILE_ROLE_C_HEADER,
    KIOTTY_SPEC_FILE_ROLE_SOURCE
} KiottySpecFileRole;

typedef enum KiottySpecLinkType {
    KIOTTY_SPEC_LINK_TYPE_SATISFIES = 1,
    KIOTTY_SPEC_LINK_TYPE_VERIFIES,
    KIOTTY_SPEC_LINK_TYPE_IMPLEMENTS,
    KIOTTY_SPEC_LINK_TYPE_COVERS,
    KIOTTY_SPEC_LINK_TYPE_DEPENDS_ON
} KiottySpecLinkType;

typedef enum KiottySpecDirection {
    KIOTTY_SPEC_DIRECTION_UPSTREAM = 1,
    KIOTTY_SPEC_DIRECTION_DOWNSTREAM,
    KIOTTY_SPEC_DIRECTION_BOTH
} KiottySpecDirection;

typedef enum KiottySpecChangeSource {
    KIOTTY_SPEC_CHANGE_SOURCE_FEEDBACK = 1,
    KIOTTY_SPEC_CHANGE_SOURCE_ISSUE
} KiottySpecChangeSource;

typedef enum KiottySpecChangeState {
    KIOTTY_SPEC_CHANGE_STATE_OPEN = 1,
    KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS,
    KIOTTY_SPEC_CHANGE_STATE_DONE,
    KIOTTY_SPEC_CHANGE_STATE_REJECTED
} KiottySpecChangeState;

typedef enum KiottySpecImplementationState {
    KIOTTY_SPEC_IMPLEMENTATION_STATE_NOT_IMPLEMENTED = 1,
    KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE,
    KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED
} KiottySpecImplementationState;

typedef enum KiottySpecTestResult {
    KIOTTY_SPEC_TEST_RESULT_PASS = 1,
    KIOTTY_SPEC_TEST_RESULT_FAIL
} KiottySpecTestResult;

typedef enum KiottySpecScope {
    KIOTTY_SPEC_SCOPE_INTRA_MODULE = 1,
    KIOTTY_SPEC_SCOPE_INTER_MODULE
} KiottySpecScope;

typedef enum KiottySpecDecisionKind {
    KIOTTY_SPEC_DECISION_KIND_REQUIREMENT = 1,
    KIOTTY_SPEC_DECISION_KIND_RULE,
    KIOTTY_SPEC_DECISION_KIND_INTERFACE,
    KIOTTY_SPEC_DECISION_KIND_DESIGN,
    KIOTTY_SPEC_DECISION_KIND_UNIT_TEST,
    KIOTTY_SPEC_DECISION_KIND_PLAN
} KiottySpecDecisionKind;

typedef enum KiottySpecCheckpointPurpose {
    KIOTTY_SPEC_CHECKPOINT_PURPOSE_NONE = 0,
    KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE
} KiottySpecCheckpointPurpose;

typedef struct KiottySpecCheckpoint {
    const char *id;
    int32_t step;
    bool confirmed;
    KiottySpecCheckpointPurpose purpose;
    const char *note;
    int64_t createdAt;
} KiottySpecCheckpoint;

typedef struct KiottySpecItem {
    const char *id;
    KiottySpecItemKind kind;
    KiottySpecItemCategory category;
    const char *parentId;
    int32_t seq;
    const char *title;
    const char *body;
    const char *cpFrom;
} KiottySpecItem;

typedef struct KiottySpecMilestoneEntry {
    const char *milestoneId;
    const char *itemId;
    KiottySpecImplementationState state;
    const char *implementedCp;
} KiottySpecMilestoneEntry;

typedef struct KiottySpecFile {
    const char *id;
    KiottySpecFileRole role;
    const char *path;
    const char *hash;
    const char *cpFrom;
} KiottySpecFile;

typedef struct KiottySpecLink {
    const char *fromId;
    const char *toId;
    KiottySpecLinkType type;
    const char *cpFrom;
} KiottySpecLink;

typedef struct KiottySpecChangeRequest {
    const char *id;
    KiottySpecChangeSource source;
    const char *body;
    int32_t entryStep;
    KiottySpecChangeState state;
    const char *cpOpened;
    const char *cpClosed;
} KiottySpecChangeRequest;

typedef struct KiottySpecResult {
    const char *targetId;
    KiottySpecTestResult result;
    const char *log;
    const char *cpFrom;
    int64_t runAt;
} KiottySpecResult;

typedef struct KiottySpecPosition {
    const char *checkpointId;
    int32_t step;
    bool confirmed;
    bool released;
    size_t pendingWrites;
    int32_t nextStep;
    const char *nextAction;
} KiottySpecPosition;

typedef struct KiottySpecStepRun {
    int32_t step;
    const char *firstCheckpointId;
    const char *lastCheckpointId;
    const char *confirmedCheckpointId;
    size_t checkpointCount;
    const char *lastNote;
    const char *reason;
} KiottySpecStepRun;

typedef struct KiottySpecItemFilter {
    KiottySpecItemKind kind;
    KiottySpecItemCategory category;
    const char *parentId;
    const char *text;
    const char *atCheckpoint;
} KiottySpecItemFilter;

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_open(const char *projectRoot, KiottySpec **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_close(KiottySpec *db);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_init(KiottySpec *db, const char *projectName, bool installGitHooks);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_setPolicy(KiottySpec *db, const char *key, const char *value);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_getPolicy(KiottySpec *db, const char *key, char **outValue);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Transaction_start(KiottySpec *db);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Transaction_commit(KiottySpec *db);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Transaction_rollback(KiottySpec *db);
KIOTTY_SPEC_API void             KiottySpec_free(void *ptr);
KIOTTY_SPEC_API const char      *KiottySpec_strerror(KiottySpecStatus status);
KIOTTY_SPEC_API const char      *KiottySpec_enumName(const char *enumType, int value);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_enumParse(const char *enumType, const char *name, int *outValue);
KIOTTY_SPEC_API const char      *KiottySpec_lastError(void);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_count(KiottySpecCursor *cursor, size_t *outCount);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getItem(KiottySpecCursor *cursor, size_t index, const KiottySpecItem **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getLink(KiottySpecCursor *cursor, size_t index, const KiottySpecLink **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getFile(KiottySpecCursor *cursor, size_t index, const KiottySpecFile **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getCheckpoint(KiottySpecCursor *cursor, size_t index, const KiottySpecCheckpoint **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getChangeRequest(KiottySpecCursor *cursor, size_t index, const KiottySpecChangeRequest **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getResult(KiottySpecCursor *cursor, size_t index, const KiottySpecResult **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getMilestoneEntry(KiottySpecCursor *cursor, size_t index, const KiottySpecMilestoneEntry **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getStepRun(KiottySpecCursor *cursor, size_t index, const KiottySpecStepRun **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Cursor_getId(KiottySpecCursor *cursor, size_t index, const char **out);
KIOTTY_SPEC_API void             KiottySpec_Cursor_end(KiottySpecCursor *cursor);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_create(KiottySpec *db, KiottySpecActor actor, int32_t step, const char *note, char **outId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_confirm(KiottySpec *db, KiottySpecActor actor, const char *cpId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_get(KiottySpec *db, const char *cpId, KiottySpecCheckpoint **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_current(KiottySpec *db, KiottySpecCheckpoint **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_lastConfirmed(KiottySpec *db, int32_t stepOr0, KiottySpecCheckpoint **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_listBegin(KiottySpec *db, int32_t stepOr0, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_changedItemsBegin(KiottySpec *db, const char *cpFrom, const char *cpToOrNull, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_diff(KiottySpec *db, const char *cpA, const char *cpB, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_confirmCheck(KiottySpec *db, int32_t step, bool *outOk, char **outReport);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_releaseCheck(KiottySpec *db, bool *outOk, char **outReport);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_release(KiottySpec *db, KiottySpecActor actor, const char *note, char **outId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_position(KiottySpec *db, KiottySpecPosition **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_journeyBegin(KiottySpec *db, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Checkpoint_prune(KiottySpec *db, KiottySpecActor actor, const char *baseCpId, size_t *outRemoved);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_create(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, KiottySpecItemCategory category,
                                                        const char *parentIdOrNull, const char *title, const char *body, char **outId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_get(KiottySpec *db, const char *itemId, KiottySpecItem **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_getAt(KiottySpec *db, const char *itemId, const char *cpId, KiottySpecItem **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_update(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *title, const char *body);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_setParent(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *parentIdOrNull, int32_t seq);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_remove(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *reason);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_exists(KiottySpec *db, const char *itemId, bool *outExists);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_listBegin(KiottySpec *db, const KiottySpecItemFilter *filter, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_childrenBegin(KiottySpec *db, const char *itemId, bool recursive, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_searchBegin(KiottySpec *db, const char *query, size_t limit, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Item_template(KiottySpecItemKind kind, KiottySpecItemCategory category, char **outMarkdown);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Milestone_setEntryState(KiottySpec *db, KiottySpecActor actor, const char *milestoneId, const char *itemId, KiottySpecImplementationState state);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Milestone_entriesBegin(KiottySpec *db, const char *milestoneId, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Milestone_uncoveredBegin(KiottySpec *db, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Milestone_next(KiottySpec *db, char **outMilestoneId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Milestone_progress(KiottySpec *db, size_t *outTotal, size_t *outImplemented, size_t *outNeedsChange);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_register(KiottySpec *db, KiottySpecActor actor, KiottySpecFileRole role, const char *path, char **outId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_get(KiottySpec *db, const char *fileId, KiottySpecFile **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_byPath(KiottySpec *db, const char *path, KiottySpecFile **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_rescan(KiottySpec *db, KiottySpecActor actor, const char *fileId, bool *outChanged);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_listBegin(KiottySpec *db, KiottySpecFileRole roleOr0, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_changedSinceBegin(KiottySpec *db, const char *cpId, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_File_scanGuiDraft(KiottySpec *db, const char *fileId, KiottySpecCursor **outUnknownIds, KiottySpecCursor **outMissingViews);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_add(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_remove(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_listBegin(KiottySpec *db, const char *id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_upstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_downstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_impactBegin(KiottySpec *db, const char *sinceCpId, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_scopeOf(KiottySpec *db, const char *designOrFileId, KiottySpecScope *outScope, KiottySpecCursor **outAffectedModules);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Link_matrix(KiottySpec *db, KiottySpecItemKind rowKind, KiottySpecItemKind colKind, char **outCsv);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_create(KiottySpec *db, KiottySpecActor actor, KiottySpecChangeSource source, const char *body, int32_t entryStep, char **outId);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_get(KiottySpec *db, const char *crId, KiottySpecChangeRequest **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_setState(KiottySpec *db, KiottySpecActor actor, const char *crId, KiottySpecChangeState state, const char *note);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_setEntryStep(KiottySpec *db, KiottySpecActor actor, const char *crId, int32_t entryStep);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_listBegin(KiottySpec *db, KiottySpecChangeState stateOr0, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_ChangeRequest_openCount(KiottySpec *db, size_t *outCount);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Decision_get(KiottySpec *db, KiottySpecDecisionKind kind, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Decision_rewrite(KiottySpec *db, KiottySpecActor actor, KiottySpecDecisionKind kind, const char *markdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Decision_check(KiottySpec *db, KiottySpecDecisionKind kind, char **outReport);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Decision_getAt(KiottySpec *db, KiottySpecDecisionKind kind, const char *cpId, char **outMarkdown);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_record(KiottySpec *db, KiottySpecActor actor, const char *targetId, KiottySpecTestResult result, const char *log);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_last(KiottySpec *db, const char *targetId, KiottySpecResult **out);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_listBegin(KiottySpec *db, const char *targetId, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_failedBegin(KiottySpec *db, KiottySpecItemKind kindOr0, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_summary(KiottySpec *db, KiottySpecItemKind kind, size_t *outTotal, size_t *outPass, size_t *outFail, size_t *outNotRun);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Result_suspectsBegin(KiottySpec *db, const char *failedTargetId, KiottySpecCursor **outCursor);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_requirementsWithoutCriteriaBegin(KiottySpec *db, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_systemTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_e2eTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_unlinkedBegin(KiottySpec *db, KiottySpecItemKind kind, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_orphanTestsBegin(KiottySpec *db, KiottySpecCursor **outCursor);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Check_all(KiottySpec *db, char **outReport);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_forStep(KiottySpec *db, int32_t step, size_t maxBytes, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_forItem(KiottySpec *db, const char *itemId, int32_t depth, size_t maxBytes, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_forMilestone(KiottySpec *db, const char *milestoneId, size_t maxBytes, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_forChangeRequest(KiottySpec *db, const char *crId, size_t maxBytes, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_rules(KiottySpec *db, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_pending(KiottySpec *db, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_whereAmI(KiottySpec *db, size_t maxBytes, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Context_status(KiottySpec *db, char **outMarkdown);

KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Export_markdown(KiottySpec *db, KiottySpecItemKind kind, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Export_traceReport(KiottySpec *db, char **outMarkdown);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Export_jsonl(KiottySpec *db, const char *dir);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Import_jsonl(KiottySpec *db, const char *dir, bool merge, char **outConflictReport);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Import_markdown(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, const char *markdown, KiottySpecCursor **outCreatedIds);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Query_readonly(KiottySpec *db, const char *sql, char **outJson);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Schema_version(KiottySpec *db, int32_t *outVersion);
KIOTTY_SPEC_API KiottySpecStatus KiottySpec_Schema_migrate(KiottySpec *db);

#ifdef __cplusplus
}
#endif

#endif
