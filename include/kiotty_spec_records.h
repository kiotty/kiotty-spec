#ifndef KIOTTY_SPEC_RECORDS_H
#define KIOTTY_SPEC_RECORDS_H

#include "kiotty_spec_types.h"

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
    uint32_t              index;
} KiottySpecTagDefinition;

typedef struct KiottySpecRelationDefinition {
    char                  name[KIOTTY_SPEC_MAX_TAG_NAME_LENGTH + 1];
    KiottySpecItemKind    target_kind;
    KiottySpecCardinality cardinality;
    uint32_t              index;
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
    KiottySpecBool decision_changed;
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

typedef struct KiottySpecTagDefinitionPage {
    KiottySpecTagDefinition elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t                  length;
} KiottySpecTagDefinitionPage;

typedef struct KiottySpecRelationDefinitionPage {
    KiottySpecRelationDefinition elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t                       length;
} KiottySpecRelationDefinitionPage;

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

#ifdef __cplusplus
static_assert(sizeof(KiottySpecText) <= 16, "unexpected padding");
static_assert(sizeof(KiottySpecBuffer) <= 16, "unexpected padding");
static_assert(sizeof(KiottySpecItemDraft) <= 56, "unexpected padding");
static_assert(sizeof(KiottySpecError) == 272, "unexpected padding");
static_assert(sizeof(KiottySpecName) == 32, "unexpected padding");
static_assert(sizeof(KiottySpecItem) == 424, "unexpected padding");
static_assert(sizeof(KiottySpecTag) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecRelation) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecCheckpoint) == 88, "unexpected padding");
static_assert(sizeof(KiottySpecStepRun) == 32, "unexpected padding");
static_assert(sizeof(KiottySpecModel) == 64, "unexpected padding");
static_assert(sizeof(KiottySpecTagDefinition) == 3144, "unexpected padding");
static_assert(sizeof(KiottySpecRelationDefinition) == 76, "unexpected padding");
static_assert(sizeof(KiottySpecTagQuery) == 16, "unexpected padding");
static_assert(sizeof(KiottySpecPosition) == 40, "unexpected padding");
static_assert(sizeof(KiottySpecUnmetCondition) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecDiffQuery) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecItemDiff) == 56, "unexpected padding");
static_assert(sizeof(KiottySpecRelationDiff) == 56, "unexpected padding");
static_assert(sizeof(KiottySpecKindDiff) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecTextDiff) == 264, "unexpected padding");
static_assert(sizeof(KiottySpecItemPage) <= 27144, "unexpected padding");
static_assert(sizeof(KiottySpecTagPage) <= 1544, "unexpected padding");
static_assert(sizeof(KiottySpecRelationPage) <= 1544, "unexpected padding");
static_assert(sizeof(KiottySpecCheckpointPage) <= 6152, "unexpected padding");
static_assert(sizeof(KiottySpecStepRunPage) <= 2056, "unexpected padding");
static_assert(sizeof(KiottySpecIdPage) <= 1032, "unexpected padding");
static_assert(sizeof(KiottySpecUnmetConditionPage) <= 1544, "unexpected padding");
static_assert(sizeof(KiottySpecItemDiffPage) <= 3592, "unexpected padding");
static_assert(sizeof(KiottySpecRelationDiffPage) <= 3592, "unexpected padding");
static_assert(sizeof(KiottySpecTextDiffPage) <= 16904, "unexpected padding");
static_assert(sizeof(KiottySpecTagDefinitionPage) <= 201224, "unexpected padding");
static_assert(sizeof(KiottySpecRelationDefinitionPage) <= 4872, "unexpected padding");
static_assert(sizeof(KiottySpecStatusResult) == 4, "unexpected padding");
static_assert(sizeof(KiottySpecSizeResult) <= 16, "unexpected padding");
static_assert(sizeof(KiottySpecIdResult) == 24, "unexpected padding");
static_assert(sizeof(KiottySpecNameResult) == 40, "unexpected padding");
static_assert(sizeof(KiottySpecErrorResult) == 280, "unexpected padding");
static_assert(sizeof(KiottySpecItemResult) == 440, "unexpected padding");
static_assert(sizeof(KiottySpecItemHandleResult) == 16, "unexpected padding");
static_assert(sizeof(KiottySpecTagResult) == 32, "unexpected padding");
static_assert(sizeof(KiottySpecCheckpointResult) == 104, "unexpected padding");
static_assert(sizeof(KiottySpecCheckpointHandleResult) == 16, "unexpected padding");
static_assert(sizeof(KiottySpecCursorResult) <= 24, "unexpected padding");
static_assert(sizeof(KiottySpecPositionResult) == 48, "unexpected padding");
static_assert(sizeof(KiottySpecModelResult) == 72, "unexpected padding");
static_assert(sizeof(KiottySpecContextResult) <= 16, "unexpected padding");
static_assert(sizeof(KiottySpecDiffSummaryResult) == 360, "unexpected padding");
static_assert(sizeof(KiottySpecConfiguration) <= 64, "unexpected padding");
#endif

#endif
