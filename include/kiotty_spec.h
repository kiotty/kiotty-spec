#ifndef KIOTTY_SPEC_H
#define KIOTTY_SPEC_H

#include "kiotty_spec_records.h"
#include "kiotty_spec_sync_records.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KIOTTY_SPEC_API
#define KIOTTY_SPEC_API
#endif

KIOTTY_SPEC_API KiottySpecContextResult
KiottySpec_createContext(const KiottySpecConfiguration *configuration);
KIOTTY_SPEC_API void KiottySpec_releaseContext(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_open(KiottySpecContext *context,
                                                       KiottySpecText path);
KIOTTY_SPEC_API KiottySpecErrorResult KiottySpec_readLastError(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_initializeProject(KiottySpecContext *context,
                                                                    KiottySpecText project_name);

KIOTTY_SPEC_API KiottySpecNameResult KiottySpec_formatStatus(KiottySpecStatus status);
KIOTTY_SPEC_API KiottySpecNameResult KiottySpec_formatKind(KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecItemKind KiottySpec_parseKind(KiottySpecText name);
KIOTTY_SPEC_API KiottySpecNameResult
KiottySpec_formatCheckpointState(KiottySpecCheckpointState state);

KIOTTY_SPEC_API KiottySpecIdResult KiottySpec_createItem(KiottySpecContext *context,
                                                         const KiottySpecItemDraft *draft);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_renameItem(KiottySpecContext *context,
                                                             KiottySpecText id,
                                                             KiottySpecText new_id);
KIOTTY_SPEC_API KiottySpecItemResult KiottySpec_readItem(KiottySpecContext *context,
                                                         KiottySpecText id);
KIOTTY_SPEC_API KiottySpecItemResult KiottySpec_readItemByHandle(KiottySpecContext *context,
                                                                 KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecItemHandleResult KiottySpec_findItem(KiottySpecContext *context,
                                                               KiottySpecText id);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_updateItem(KiottySpecContext *context,
                                                             KiottySpecText id,
                                                             KiottySpecText title,
                                                             KiottySpecText summary);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_setItemParent(KiottySpecContext *context,
                                                                KiottySpecText id,
                                                                KiottySpecText parent_id);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_removeItem(KiottySpecContext *context,
                                                             KiottySpecText id);

KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_writeItemBody(KiottySpecContext *context,
                                                                KiottySpecText id,
                                                                KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext *context,
                                                             KiottySpecText id,
                                                             KiottySpecBuffer buffer);

KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_setItemTag(KiottySpecContext *context,
                                                             KiottySpecItemHandle item,
                                                             uint32_t tag, uint64_t value);
KIOTTY_SPEC_API KiottySpecTagResult KiottySpec_readItemTag(KiottySpecContext *context,
                                                           KiottySpecItemHandle item, uint32_t tag);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openTagCursor(KiottySpecContext *context,
                                                                KiottySpecItemHandle item);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchTagPage(KiottySpecContext *context,
                                                               KiottySpecCursor cursor,
                                                               KiottySpecTagPage *page);

KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_addRelation(KiottySpecContext *context,
                                                              KiottySpecText from_id,
                                                              uint32_t relation,
                                                              KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_removeRelation(KiottySpecContext *context,
                                                                 KiottySpecText from_id,
                                                                 uint32_t relation,
                                                                 KiottySpecText to_id);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openRelationCursor(KiottySpecContext *context,
                                                                     KiottySpecText id,
                                                                     KiottySpecDirection direction);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openRelationCursorByModel(KiottySpecContext *context, KiottySpecItemKind kind,
                                     uint32_t relation);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchRelationPage(KiottySpecContext *context,
                                                                    KiottySpecCursor cursor,
                                                                    KiottySpecRelationPage *page);

KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openItemCursor(KiottySpecContext *context, KiottySpecItemKind kind,
                          const KiottySpecTagQuery *tag_queries, size_t count);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openChildCursor(KiottySpecContext *context,
                                                                  KiottySpecText id);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openImpactCursor(KiottySpecContext *context,
                                                                   KiottySpecText id,
                                                                   uint32_t depth);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchItemPage(KiottySpecContext *context,
                                                                KiottySpecCursor cursor,
                                                                KiottySpecItemPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchIdPage(KiottySpecContext *context,
                                                              KiottySpecCursor cursor,
                                                              KiottySpecIdPage *page);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_closeCursor(KiottySpecContext *context,
                                                              KiottySpecCursor cursor);

KIOTTY_SPEC_API KiottySpecCheckpointHandleResult
KiottySpec_startCheckpoint(KiottySpecContext *context, KiottySpecStep step,
                           KiottySpecText description);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_resumeCheckpoint(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_completeCheckpoint(KiottySpecContext *context,
                                                                     KiottySpecActor actor);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openUnmetConditionCursor(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult
KiottySpec_fetchUnmetConditionPage(KiottySpecContext *context, KiottySpecCursor cursor,
                                   KiottySpecUnmetConditionPage *page);
KIOTTY_SPEC_API KiottySpecCheckpointResult
KiottySpec_readCheckpoint(KiottySpecContext *context, KiottySpecCheckpointHandle handle);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openCheckpointCursor(KiottySpecContext *context,
                                                                       KiottySpecStep step);
KIOTTY_SPEC_API KiottySpecStatusResult
KiottySpec_fetchCheckpointPage(KiottySpecContext *context, KiottySpecCursor cursor,
                               KiottySpecCheckpointPage *page);
KIOTTY_SPEC_API KiottySpecSizeResult KiottySpec_pruneCheckpoints(KiottySpecContext *context,
                                                                 KiottySpecActor actor,
                                                                 KiottySpecCheckpointHandle base);

KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_writeDecision(KiottySpecContext *context,
                                                                KiottySpecItemKind kind,
                                                                KiottySpecText text);
KIOTTY_SPEC_API KiottySpecSizeResult KiottySpec_readDecision(KiottySpecContext *context,
                                                             KiottySpecItemKind kind,
                                                             KiottySpecBuffer buffer);

KIOTTY_SPEC_API KiottySpecModelResult KiottySpec_readModel(KiottySpecContext *context,
                                                           KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openTagDefinitionCursor(KiottySpecContext *context, KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecStatusResult
KiottySpec_fetchTagDefinitionPage(KiottySpecContext *context, KiottySpecCursor cursor,
                                  KiottySpecTagDefinitionPage *page);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openRelationDefinitionCursor(KiottySpecContext *context, KiottySpecItemKind kind);
KIOTTY_SPEC_API KiottySpecStatusResult
KiottySpec_fetchRelationDefinitionPage(KiottySpecContext *context, KiottySpecCursor cursor,
                                       KiottySpecRelationDefinitionPage *page);

KIOTTY_SPEC_API KiottySpecPositionResult KiottySpec_readPosition(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openStepRunCursor(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchStepRunPage(KiottySpecContext *context,
                                                                   KiottySpecCursor cursor,
                                                                   KiottySpecStepRunPage *page);

KIOTTY_SPEC_API KiottySpecDiffSummaryResult
KiottySpec_summarizeDiff(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openItemDiffCursor(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchItemDiffPage(KiottySpecContext *context,
                                                                    KiottySpecCursor cursor,
                                                                    KiottySpecItemDiffPage *page);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openRelationDiffCursor(KiottySpecContext *context, const KiottySpecDiffQuery *query);
KIOTTY_SPEC_API KiottySpecStatusResult
KiottySpec_fetchRelationDiffPage(KiottySpecContext *context, KiottySpecCursor cursor,
                                 KiottySpecRelationDiffPage *page);
KIOTTY_SPEC_API KiottySpecItemResult KiottySpec_readItemAt(KiottySpecContext *context,
                                                           KiottySpecText id,
                                                           KiottySpecCheckpointHandle checkpoint);
KIOTTY_SPEC_API KiottySpecTagResult KiottySpec_readItemTagAt(KiottySpecContext *context,
                                                             KiottySpecText id, uint32_t tag,
                                                             KiottySpecCheckpointHandle checkpoint);
KIOTTY_SPEC_API KiottySpecSizeResult
KiottySpec_readItemBodyAt(KiottySpecContext *context, KiottySpecText id,
                          KiottySpecCheckpointHandle checkpoint, KiottySpecBuffer buffer);
KIOTTY_SPEC_API KiottySpecSizeResult
KiottySpec_readDecisionAt(KiottySpecContext *context, KiottySpecItemKind kind,
                          KiottySpecCheckpointHandle checkpoint, KiottySpecBuffer buffer);
KIOTTY_SPEC_API KiottySpecCursorResult
KiottySpec_openTextDiffCursor(KiottySpecContext *context, KiottySpecItemHandle item,
                              KiottySpecTextField field, const KiottySpecDiffQuery *range);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchTextDiffPage(KiottySpecContext *context,
                                                                    KiottySpecCursor cursor,
                                                                    KiottySpecTextDiffPage *page);

KIOTTY_SPEC_API KiottySpecRemoteResult KiottySpec_readRemote(KiottySpecContext *context);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_writeRemote(KiottySpecContext *context,
                                                              const KiottySpecRemote *remote);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openRuleDiffCursor(KiottySpecContext *local,
                                                                     KiottySpecContext *remote);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_fetchRuleDiffPage(KiottySpecContext *context,
                                                                    KiottySpecCursor cursor,
                                                                    KiottySpecRuleDiffPage *page);
KIOTTY_SPEC_API KiottySpecCursorResult KiottySpec_openRuleBodyDiffCursor(KiottySpecContext *local,
                                                                         KiottySpecContext *remote,
                                                                         KiottySpecText id);
KIOTTY_SPEC_API KiottySpecAcceptResult KiottySpec_acceptRules(KiottySpecContext *local,
                                                              KiottySpecContext *remote,
                                                              const KiottySpecAcceptInput *input,
                                                              KiottySpecActor actor);
KIOTTY_SPEC_API KiottySpecStatusResult KiottySpec_exportRules(KiottySpecContext *local,
                                                              KiottySpecText path);

#ifdef __cplusplus
}
#endif

#endif
