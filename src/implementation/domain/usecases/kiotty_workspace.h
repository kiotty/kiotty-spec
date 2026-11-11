#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"
#include "implementation/domain/usecases/kiotty_spec_file.h"

namespace kiotty::spec::implementation {

struct Workspace {
    SpecFile& file;
    entity::Failure& failure;
    const ModelCatalog& catalog;
    MetaStore& meta;
    CheckpointStore& checkpoints;
    ItemStore& items;
    RelationStore& relations;
    DecisionStore& decisions;

    KiottySpecStatus reload() { return file.load(); }

    KiottySpecStatus refresh()
    {
        const KiottySpecStatus unchanged = file.checkUnchanged();
        if (unchanged == KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
        const KiottySpecStatus reloaded = file.load();
        return reloaded == KIOTTY_SPEC_STATUS_SUCCESS ? unchanged : reloaded;
    }

    KiottySpecStatus finish(KiottySpecStatus status)
    {
        const KiottySpecStatus result = status == KIOTTY_SPEC_STATUS_SUCCESS ? file.save() : status;
        if (result == KIOTTY_SPEC_STATUS_SUCCESS) { return result; }
        const KiottySpecStatus reloaded = file.load();
        return reloaded == KIOTTY_SPEC_STATUS_SUCCESS ? result : reloaded;
    }

    Result<entity::ItemHandle, KiottySpecStatus> resolve(Span<const char> id)
    {
        const auto found = items.reader().find(id);
        if (!found) {
            return Result<entity::ItemHandle, KiottySpecStatus>::failure(
                failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id));
        }
        return found;
    }
};

}
