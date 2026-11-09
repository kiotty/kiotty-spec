#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"

namespace kiotty::spec::implementation {

// Context 가 소유한 store 와 모델 표를 usecase 에 빌려주는 참조 묶음.
struct Workspace {
    Storage& storage;
    Failure& failure;
    const ModelCatalog& catalog;
    MetaStore& meta;
    CheckpointStore& checkpoints;
    ItemStore& items;
    RelationStore& relations;
    DecisionStore& decisions;

    // DB 의 현재 버전을 풀에 다시 적재한다.
    KiottySpecStatus reload()
    {
        KiottySpecStatus status = checkpoints.load();
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = items.load(); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = relations.load(); }
        return status;
    }

    // 없으면 NOT_FOUND 와 이유를 남긴다.
    Result<entity::ItemHandle, KiottySpecStatus> resolve(Span<const char> id)
    {
        const auto found = items.reader().find(id);
        if (!found) { return Result<entity::ItemHandle, KiottySpecStatus>::failure(failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 항목: "), id})); }
        return found;
    }
};

}
