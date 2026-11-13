#ifndef KIOTTY_SPEC_SYNC_RECORDS_H
#define KIOTTY_SPEC_SYNC_RECORDS_H

#include "kiotty_spec_records.h"

typedef struct KiottySpecAcceptInput {
    KiottySpecText description;
    KiottySpecText title;
    KiottySpecText summary;
    KiottySpecText body;
    KiottySpecText rule_base;
} KiottySpecAcceptInput;

typedef struct KiottySpecRemote {
    char url[KIOTTY_SPEC_MAX_URL_LENGTH + 1];
    char rule_base[KIOTTY_SPEC_MAX_COMMIT_LENGTH + 1];
} KiottySpecRemote;

typedef struct KiottySpecRuleDiff {
    char             title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1];
    char             id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecChange change;
    uint32_t         changed_fields;
} KiottySpecRuleDiff;

typedef struct KiottySpecRuleDiffPage {
    KiottySpecRuleDiff elements[KIOTTY_SPEC_PAGE_CAPACITY];
    size_t             length;
} KiottySpecRuleDiffPage;

typedef struct KiottySpecRemoteResult {
    KiottySpecRemote value;
    KiottySpecStatus code;
    uint32_t         reserved;
} KiottySpecRemoteResult;

typedef struct KiottySpecAcceptResult {
    KiottySpecCheckpointHandle checkpoint;
    char                       change_request[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    KiottySpecStatus           code;
    uint32_t                   reserved;
} KiottySpecAcceptResult;

#ifdef __cplusplus
static_assert(sizeof(KiottySpecAcceptInput) <= 80, "unexpected padding");
static_assert(sizeof(KiottySpecRemote) == 577, "unexpected padding");
static_assert(sizeof(KiottySpecRuleDiff) == 152, "unexpected padding");
static_assert(sizeof(KiottySpecRuleDiffPage) <= 9736, "unexpected padding");
static_assert(sizeof(KiottySpecRemoteResult) == 588, "unexpected padding");
static_assert(sizeof(KiottySpecAcceptResult) == 32, "unexpected padding");
#endif

#endif
