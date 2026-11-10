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

// Context 가 소유한 store 와 모델 표를 usecase 에 빌려주는 참조 묶음.
struct Workspace {
    SpecFile& file;
    entity::Failure& failure;
    const ModelCatalog& catalog;
    MetaStore& meta;
    CheckpointStore& checkpoints;
    ItemStore& items;
    RelationStore& relations;
    DecisionStore& decisions;

    // 파일을 다시 읽어 버전 표와 현재 뷰를 만든다.
    KiottySpecStatus reload() { return file.load(); }

    // 쓰기를 시작하기 전에 부른다. 파일이 밖에서 바뀌었으면 다시 읽고 CONFLICT 다. 낡은 내용으로 검사하지 않게 한다.
    KiottySpecStatus refresh()
    {
        const KiottySpecStatus unchanged = file.checkUnchanged();
        if (unchanged == KIOTTY_SPEC_STATUS_SUCCESS) { return unchanged; }
        const KiottySpecStatus reloaded = file.load();
        return reloaded == KIOTTY_SPEC_STATUS_SUCCESS ? unchanged : reloaded;
    }

    // 쓰기 하나를 끝낸다. 성공이면 파일에 옮기고, 실패면 파일에서 다시 읽어 메모리의 변경을 버린다.
    KiottySpecStatus finish(KiottySpecStatus status)
    {
        const KiottySpecStatus result = status == KIOTTY_SPEC_STATUS_SUCCESS ? file.save() : status;
        if (result == KIOTTY_SPEC_STATUS_SUCCESS) { return result; }
        const KiottySpecStatus reloaded = file.load();
        return reloaded == KIOTTY_SPEC_STATUS_SUCCESS ? result : reloaded;
    }

    // 없으면 NOT_FOUND 와 이유를 남긴다.
    Result<entity::ItemHandle, KiottySpecStatus> resolve(Span<const char> id)
    {
        const auto found = items.reader().find(id);
        if (!found) { return Result<entity::ItemHandle, KiottySpecStatus>::failure(failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_ITEM, id)); }
        return found;
    }
};

}
