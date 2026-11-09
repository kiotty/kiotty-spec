#pragma once

#include "kiotty_spec.h"
#include "implementation/core/failure.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/checkpoint_store.h"
#include "implementation/data/repository/decision_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/meta_store.h"
#include "implementation/data/repository/relation_store.h"
#include "implementation/domain/usecases/model_catalog.h"

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
};

}
