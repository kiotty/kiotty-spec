#ifndef KIOTTY_SPEC_H
#define KIOTTY_SPEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KIOTTY_SPEC_API
#define KIOTTY_SPEC_API
#endif

#define KIOTTY_SPEC_MAX_ID_LENGTH          15
#define KIOTTY_SPEC_MAX_TITLE_LENGTH       127
#define KIOTTY_SPEC_MAX_SUMMARY_LENGTH     255
#define KIOTTY_SPEC_MAX_BODY_LENGTH        16384
#define KIOTTY_SPEC_MAX_DECISION_LENGTH    2048
#define KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH 63
#define KIOTTY_SPEC_MAX_MODEL_NAME_LENGTH  31
#define KIOTTY_SPEC_MAX_TAG_NAME_LENGTH    63
#define KIOTTY_SPEC_MAX_NAME_LENGTH        31
#define KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH  255
#define KIOTTY_SPEC_MAX_PROJECT_NAME_LENGTH 255
#define KIOTTY_SPEC_TAG_BIT_COUNT          48
#define KIOTTY_SPEC_MAX_TAGS_PER_MODEL     256
#define KIOTTY_SPEC_MAX_TAG_QUERIES        4
#define KIOTTY_SPEC_PAGE_CAPACITY          64
#define KIOTTY_SPEC_ITEM_KIND_COUNT        13
#define KIOTTY_SPEC_STEP_COUNT             12
#define KIOTTY_SPEC_CHECKPOINT_CURRENT     0
#define KIOTTY_SPEC_MAX_POOL_CAPACITY      1048576
#define KIOTTY_SPEC_MAX_CURSOR_CAPACITY    1024
#define KIOTTY_SPEC_MAX_BUFFER_BYTES       1073741824
#define KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES  99328

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

typedef enum KiottySpecSubject {
    KIOTTY_SPEC_SUBJECT_NONE = 0,
    KIOTTY_SPEC_SUBJECT_FILE,
    KIOTTY_SPEC_SUBJECT_FILE_LINE,
    KIOTTY_SPEC_SUBJECT_MERGE_MARKER,
    KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE,
    KIOTTY_SPEC_SUBJECT_FILE_CHANGED,
    KIOTTY_SPEC_SUBJECT_FILE_FORMAT,
    KIOTTY_SPEC_SUBJECT_CONFIGURATION,
    KIOTTY_SPEC_SUBJECT_MODEL,
    KIOTTY_SPEC_SUBJECT_KIND,
    KIOTTY_SPEC_SUBJECT_ITEM,
    KIOTTY_SPEC_SUBJECT_ID,
    KIOTTY_SPEC_SUBJECT_ID_IN_HISTORY,
    KIOTTY_SPEC_SUBJECT_TITLE,
    KIOTTY_SPEC_SUBJECT_SUMMARY,
    KIOTTY_SPEC_SUBJECT_BODY,
    KIOTTY_SPEC_SUBJECT_PAST_BODY,
    KIOTTY_SPEC_SUBJECT_PARENT,
    KIOTTY_SPEC_SUBJECT_CHILD,
    KIOTTY_SPEC_SUBJECT_TAG,
    KIOTTY_SPEC_SUBJECT_TAG_VALUE,
    KIOTTY_SPEC_SUBJECT_RELATION,
    KIOTTY_SPEC_SUBJECT_DECISION,
    KIOTTY_SPEC_SUBJECT_CHECKPOINT,
    KIOTTY_SPEC_SUBJECT_DESCRIPTION,
    KIOTTY_SPEC_SUBJECT_STEP,
    KIOTTY_SPEC_SUBJECT_ACTOR,
    KIOTTY_SPEC_SUBJECT_CURSOR,
    KIOTTY_SPEC_SUBJECT_CURSOR_ROWS,
    KIOTTY_SPEC_SUBJECT_DIFF_RANGE,
    KIOTTY_SPEC_SUBJECT_TEXT_FIELD,
    KIOTTY_SPEC_SUBJECT_PROJECT_NAME,
    KIOTTY_SPEC_SUBJECT_COMPLETION
} KiottySpecSubject;

typedef enum KiottySpecActor {
    KIOTTY_SPEC_ACTOR_USER = 1,
    KIOTTY_SPEC_ACTOR_AI
} KiottySpecActor;

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

typedef enum KiottySpecCheckpointState {
    KIOTTY_SPEC_CHECKPOINT_STATE_STARTED = 1,
    KIOTTY_SPEC_CHECKPOINT_STATE_RUNNING,
    KIOTTY_SPEC_CHECKPOINT_STATE_DONE
} KiottySpecCheckpointState;

typedef enum KiottySpecCardinality {
    KIOTTY_SPEC_CARDINALITY_MANY = 1,
    KIOTTY_SPEC_CARDINALITY_SINGLE
} KiottySpecCardinality;

typedef enum KiottySpecDirection {
    KIOTTY_SPEC_DIRECTION_UPSTREAM = 1,
    KIOTTY_SPEC_DIRECTION_DOWNSTREAM,
    KIOTTY_SPEC_DIRECTION_BOTH
} KiottySpecDirection;

typedef enum KiottySpecCondition {
    KIOTTY_SPEC_CONDITION_ENTRY_STEP = 1,
    KIOTTY_SPEC_CONDITION_ACCEPTANCE_CRITERIA,
    KIOTTY_SPEC_CONDITION_MILESTONE_COVERAGE,
    KIOTTY_SPEC_CONDITION_SYSTEM_TEST_PAIR,
    KIOTTY_SPEC_CONDITION_END_TO_END_TEST_PAIR,
    KIOTTY_SPEC_CONDITION_CODE_INSPECTION_PAIR,
    KIOTTY_SPEC_CONDITION_SINGLE_TARGET,
    KIOTTY_SPEC_CONDITION_PASSING_RESULT,
    KIOTTY_SPEC_CONDITION_CLOSED_CHANGE_REQUEST
} KiottySpecCondition;

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

typedef enum KiottySpecTextField {
    KIOTTY_SPEC_TEXT_FIELD_TITLE   = KIOTTY_SPEC_CHANGED_FIELD_TITLE,
    KIOTTY_SPEC_TEXT_FIELD_SUMMARY = KIOTTY_SPEC_CHANGED_FIELD_SUMMARY
} KiottySpecTextField;

typedef enum KiottySpecTextChange {
    KIOTTY_SPEC_TEXT_CHANGE_UNCHANGED = 0,
    KIOTTY_SPEC_TEXT_CHANGE_ADDED,
    KIOTTY_SPEC_TEXT_CHANGE_REMOVED
} KiottySpecTextChange;

typedef struct KiottySpecContext KiottySpecContext;

typedef uint64_t KiottySpecItemHandle;
typedef uint64_t KiottySpecCheckpointHandle;
typedef uint64_t KiottySpecCursor;

#define KIOTTY_SPEC_INVALID_ITEM_HANDLE ((KiottySpecItemHandle)0xFFFFFFFFu)

typedef struct KiottySpecText {
    const char *data;
    size_t      length;
} KiottySpecText;

typedef struct KiottySpecBuffer {
    char  *data;
    size_t capacity;
} KiottySpecBuffer;

typedef struct KiottySpecItemDraft {
    KiottySpecText     id;
    KiottySpecText     title;
    KiottySpecText     summary;
    KiottySpecItemKind kind;
    uint32_t           reserved;
} KiottySpecItemDraft;

typedef struct KiottySpecError {
    char              text[KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH + 1];
    uint64_t          line;
    KiottySpecStatus  status;
    KiottySpecSubject subject;
} KiottySpecError;

typedef struct KiottySpecName {
    char value[KIOTTY_SPEC_MAX_NAME_LENGTH + 1];
} KiottySpecName;

typedef struct KiottySpecItem {
    char                       summary[KIOTTY_SPEC_MAX_SUMMARY_LENGTH + 1];
    char                       title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1];
    char                       id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecCheckpointHandle checkpoint;
    KiottySpecItemHandle       parent;
    KiottySpecItemKind         kind;
    uint32_t                   reserved;
} KiottySpecItem;

typedef struct KiottySpecTag {
    KiottySpecItemHandle item;
    uint64_t             value;
    KiottySpecItemKind   model;
    uint32_t             tag;
} KiottySpecTag;

typedef struct KiottySpecRelation {
    KiottySpecItemHandle from;
    KiottySpecItemHandle to;
    KiottySpecItemKind   model;
    uint32_t             relation;
} KiottySpecRelation;

typedef struct KiottySpecCheckpoint {
    char                       description[KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH + 1];
    int64_t                    created_at;
    KiottySpecCheckpointHandle previous;
    KiottySpecStep             step;
    KiottySpecCheckpointState  state;
} KiottySpecCheckpoint;

typedef struct KiottySpecStepRun {
    KiottySpecCheckpointHandle first;
    KiottySpecCheckpointHandle last;
    uint64_t                   count;
    KiottySpecStep             step;
    KiottySpecCheckpointState  state;
} KiottySpecStepRun;

typedef struct KiottySpecModel {
    char               name[KIOTTY_SPEC_MAX_MODEL_NAME_LENGTH + 1];
    char               id_prefix[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecItemKind kind;
    KiottySpecStep     created_in;
    uint32_t           tag_count;
    uint32_t           relation_count;
} KiottySpecModel;

typedef struct KiottySpecTagDefinition {
    char                  bit_names[KIOTTY_SPEC_TAG_BIT_COUNT][KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    char                  name[KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    KiottySpecCardinality cardinality;
} KiottySpecTagDefinition;

typedef struct KiottySpecRelationDefinition {
    char                  name[KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    KiottySpecItemKind    target_kind;
    KiottySpecCardinality cardinality;
} KiottySpecRelationDefinition;

typedef struct KiottySpecTagQuery {
    uint64_t           mask;
    KiottySpecItemKind kind;
    uint32_t           tag_index;
} KiottySpecTagQuery;

typedef struct KiottySpecPosition {
    KiottySpecCheckpointHandle current;
    uint64_t                   pending_writes;
    uint64_t                   open_change_requests;
    KiottySpecStep             step;
    KiottySpecCheckpointState  state;
    KiottySpecStep             next_step;
    uint32_t                   reserved;
} KiottySpecPosition;

typedef struct KiottySpecUnmetCondition {
    char                id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecCondition condition;
    uint32_t            reserved;
} KiottySpecUnmetCondition;

typedef struct KiottySpecDiffQuery {
    KiottySpecCheckpointHandle from;
    KiottySpecCheckpointHandle to;
    uint32_t                   kind_mask;
    uint32_t                   field_mask;
} KiottySpecDiffQuery;

typedef struct KiottySpecItemDiff {
    char                       id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecCheckpointHandle before;
    KiottySpecCheckpointHandle after;
    KiottySpecItemHandle       item;
    KiottySpecItemKind         kind;
    KiottySpecChange           change;
    uint32_t                   changed_fields;
    uint32_t                   reserved;
} KiottySpecItemDiff;

typedef struct KiottySpecRelationDiff {
    char                       from_id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char                       to_id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecCheckpointHandle checkpoint;
    KiottySpecItemKind         model;
    uint32_t                   relation;
    KiottySpecChange           change;
    uint32_t                   reserved;
} KiottySpecRelationDiff;

typedef struct KiottySpecKindDiff {
    uint32_t added;
    uint32_t modified;
    uint32_t removed;
    uint32_t relations_added;
    uint32_t relations_removed;
    uint32_t decision_changed;
} KiottySpecKindDiff;

typedef struct KiottySpecTextDiff {
    char                 text[KIOTTY_SPEC_MAX_SUMMARY_LENGTH + 1];
    uint32_t             length;
    KiottySpecTextChange change;
} KiottySpecTextDiff;

typedef struct KiottySpecItemPage {
    KiottySpecItem elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t         length;
} KiottySpecItemPage;

typedef struct KiottySpecTagPage {
    KiottySpecTag elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t        length;
} KiottySpecTagPage;

typedef struct KiottySpecRelationPage {
    KiottySpecRelation elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t             length;
} KiottySpecRelationPage;

typedef struct KiottySpecCheckpointPage {
    KiottySpecCheckpoint       elements[KIOTTY_SPEC_PAGE_CAPACITY];
    KiottySpecCheckpointHandle handles[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t                     length;
} KiottySpecCheckpointPage;

typedef struct KiottySpecStepRunPage {
    KiottySpecStepRun elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t            length;
} KiottySpecStepRunPage;

typedef struct KiottySpecIdPage {
    char   elements[KIOTTY_SPEC_PAGE_CAPACITY][KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    size_t length;
} KiottySpecIdPage;

typedef struct KiottySpecUnmetConditionPage {
    KiottySpecUnmetCondition elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t                   length;
} KiottySpecUnmetConditionPage;

typedef struct KiottySpecItemDiffPage {
    KiottySpecItemDiff elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t             length;
} KiottySpecItemDiffPage;

typedef struct KiottySpecRelationDiffPage {
    KiottySpecRelationDiff elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t                 length;
} KiottySpecRelationDiffPage;

typedef struct KiottySpecTextDiffPage {
    KiottySpecTextDiff elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t             length;
} KiottySpecTextDiffPage;

typedef struct KiottySpecStatusResult {
    KiottySpecStatus code;
} KiottySpecStatusResult;

typedef struct KiottySpecSizeResult {
    size_t           value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecSizeResult;

typedef struct KiottySpecIdResult {
    char             value[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecIdResult;

typedef struct KiottySpecNameResult {
    KiottySpecName   value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecNameResult;

typedef struct KiottySpecErrorResult {
    KiottySpecError  value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecErrorResult;

typedef struct KiottySpecItemResult {
    KiottySpecItem       value;
    KiottySpecItemHandle handle;
    KiottySpecStatus     code;
    uint32_t             reserved;
} KiottySpecItemResult;

typedef struct KiottySpecItemHandleResult {
    KiottySpecItemHandle value;
    KiottySpecStatus     code;
    uint32_t             reserved;
} KiottySpecItemHandleResult;

typedef struct KiottySpecTagResult {
    KiottySpecTag    value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecTagResult;

typedef struct KiottySpecCheckpointResult {
    KiottySpecCheckpoint       value;
    KiottySpecCheckpointHandle handle;
    KiottySpecStatus           code;
    uint32_t                   reserved;
} KiottySpecCheckpointResult;

typedef struct KiottySpecCheckpointHandleResult {
    KiottySpecCheckpointHandle value;
    KiottySpecStatus           code;
    uint32_t                   reserved;
} KiottySpecCheckpointHandleResult;

typedef struct KiottySpecCursorResult {
    KiottySpecCursor value;
    size_t           total;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecCursorResult;

typedef struct KiottySpecPositionResult {
    KiottySpecPosition value;
    KiottySpecStatus   code;
    uint32_t           reserved;
} KiottySpecPositionResult;

typedef struct KiottySpecModelResult {
    KiottySpecModel  value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecModelResult;

typedef struct KiottySpecRelationDefinitionResult {
    KiottySpecRelationDefinition value;
    KiottySpecStatus             code;
    uint32_t                     reserved;
} KiottySpecRelationDefinitionResult;

typedef struct KiottySpecContextResult {
    KiottySpecContext *value;
    KiottySpecStatus   code;
    uint32_t           reserved;
} KiottySpecContextResult;

typedef struct KiottySpecDiffSummaryResult {
    KiottySpecKindDiff         kinds[KIOTTY_SPEC_ITEM_KIND_COUNT];
    KiottySpecCheckpointHandle from;
    KiottySpecCheckpointHandle to;
    KiottySpecStatus           code;
    uint32_t                   reserved;
} KiottySpecDiffSummaryResult;

typedef struct KiottySpecConfiguration {
    size_t max_items;
    size_t max_relations;
    size_t max_tags;
    size_t max_checkpoints;
    size_t max_cursors;
    size_t max_cursor_rows;
    size_t work_buffer_bytes;
    size_t max_body_bytes;
} KiottySpecConfiguration;

KIOTTY_SPEC_API KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration *configuration);
KIOTTY_SPEC_API void                    KiottySpec_releaseContext(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult  KiottySpec_open(KiottySpecContext *context, KiottySpecText path);
KIOTTY_SPEC_API KiottySpecErrorResult   KiottySpec_readLastError(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult  KiottySpec_initializeProject(KiottySpecContext *context, KiottySpecText project_name);

KIOTTY_SPEC_API KiottySpecNameResult    KiottySpec_formatStatus(KiottySpecStatus status);
KIOTTY_SPEC_API KiottySpecNameResult    KiottySpec_formatKind(KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecItemKind      KiottySpec_parseKind(KiottySpecText name);
KIOTTY_SPEC_API KiottySpecNameResult    KiottySpec_formatCheckpointState(KiottySpecCheckpointState state);

KIOTTY_SPEC_API KiottySpecIdResult         KiottySpec_createItem(KiottySpecContext *context, const KiottySpecItemDraft *draft);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_renameItem(KiottySpecContext *context, KiottySpecText id, KiottySpecText new_id);
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItem(KiottySpecContext *context, KiottySpecText id);
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItemByHandle(KiottySpecContext *context, KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext *context, KiottySpecText id);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_updateItem(KiottySpecContext *context, KiottySpecText id, KiottySpecText title, KiottySpecText summary);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_setItemParent(KiottySpecContext *context, KiottySpecText id, KiottySpecText parent_id);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_removeItem(KiottySpecContext *context, KiottySpecText id);

KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_writeItemBody(KiottySpecContext *context, KiottySpecText id, KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readItemBody(KiottySpecContext *context, KiottySpecText id, KiottySpecBuffer buffer);

KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_setItemTag(KiottySpecContext *context, KiottySpecItemHandle item, uint32_t tag, uint64_t value);
KIOTTY_SPEC_API KiottySpecTagResult        KiottySpec_readItemTag(KiottySpecContext *context, KiottySpecItemHandle item, uint32_t tag);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openTagCursor(KiottySpecContext *context, KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchTagPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecTagPage *page);

KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_addRelation(KiottySpecContext *context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_removeRelation(KiottySpecContext *context, KiottySpecText from_id, uint32_t relation, KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationCursor(KiottySpecContext *context, KiottySpecText id, KiottySpecDirection direction);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationCursorByModel(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t relation);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchRelationPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecRelationPage *page);

KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openItemCursor(KiottySpecContext *context, KiottySpecItemKind kind, const KiottySpecTagQuery *tag_queries, size_t count);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openChildCursor(KiottySpecContext *context, KiottySpecText id);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openImpactCursor(KiottySpecContext *context, KiottySpecText id, uint32_t depth);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchItemPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecItemPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchIdPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecIdPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_closeCursor(KiottySpecContext *context, KiottySpecCursor cursor);

KIOTTY_SPEC_API KiottySpecCheckpointHandleResult KiottySpec_startCheckpoint(KiottySpecContext *context, KiottySpecStep step, KiottySpecText description);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_resumeCheckpoint(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_completeCheckpoint(KiottySpecContext *context, KiottySpecActor actor);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openUnmetConditionCursor(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchUnmetConditionPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecUnmetConditionPage *page);
KIOTTY_SPEC_API KiottySpecCheckpointResult KiottySpec_readCheckpoint(KiottySpecContext *context, KiottySpecCheckpointHandle handle);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openCheckpointCursor(KiottySpecContext *context, KiottySpecStep step);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchCheckpointPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecCheckpointPage *page);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_pruneCheckpoints(KiottySpecContext *context, KiottySpecActor actor, KiottySpecCheckpointHandle base);

KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_writeDecision(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readDecision(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecBuffer buffer);

KIOTTY_SPEC_API KiottySpecModelResult      KiottySpec_readModel(KiottySpecContext *context, KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_readTagDefinition(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t index, KiottySpecTagDefinition *definition);
KIOTTY_SPEC_API KiottySpecRelationDefinitionResult KiottySpec_readRelationDefinition(KiottySpecContext *context, KiottySpecItemKind kind, uint32_t index);

KIOTTY_SPEC_API KiottySpecPositionResult   KiottySpec_readPosition(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openStepRunCursor(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchStepRunPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecStepRunPage *page);

KIOTTY_SPEC_API KiottySpecDiffSummaryResult KiottySpec_summarizeDiff(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openItemDiffCursor(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchItemDiffPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecItemDiffPage *page);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openRelationDiffCursor(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchRelationDiffPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecRelationDiffPage *page);
KIOTTY_SPEC_API KiottySpecItemResult       KiottySpec_readItemAt(KiottySpecContext *context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint);
KIOTTY_SPEC_API KiottySpecTagResult        KiottySpec_readItemTagAt(KiottySpecContext *context, KiottySpecText id, uint32_t tag, KiottySpecCheckpointHandle checkpoint);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readItemBodyAt(KiottySpecContext *context, KiottySpecText id, KiottySpecCheckpointHandle checkpoint, KiottySpecBuffer buffer);
KIOTTY_SPEC_API KiottySpecSizeResult       KiottySpec_readDecisionAt(KiottySpecContext *context, KiottySpecItemKind kind, KiottySpecCheckpointHandle checkpoint, KiottySpecBuffer buffer);
KIOTTY_SPEC_API KiottySpecCursorResult     KiottySpec_openTextDiffCursor(KiottySpecContext *context, KiottySpecItemHandle item, KiottySpecTextField field, const KiottySpecDiffQuery *range);
KIOTTY_SPEC_API KiottySpecStatusResult     KiottySpec_fetchTextDiffPage(KiottySpecContext *context, KiottySpecCursor cursor, KiottySpecTextDiffPage *page);

#ifdef __cplusplus
}
#endif

#endif
