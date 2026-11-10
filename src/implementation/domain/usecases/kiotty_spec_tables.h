#pragma once

#include "implementation/core/kiotty_failure.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"

#include <string_view>

namespace kiotty::spec::implementation {

// .kisp 파일 형식 (docs/plan/261110_kisp_v1.md). 모델이 바뀌어 이름이 달라지면 MODEL_VERSION 을 올린다.
inline constexpr std::string_view KISP_HEADER = "#kisp 1 model 1";
inline constexpr std::string_view TABLE_MARKER = "TABLE";
inline constexpr std::string_view LIVE_MARKER = "O";
inline constexpr std::string_view REMOVED_MARKER = "X";
inline constexpr std::string_view NONE_MARKER = "-";

inline constexpr std::string_view META_TABLE = "meta";
inline constexpr std::string_view CHECKPOINT_TABLE = "checkpoint";
inline constexpr std::string_view ITEM_TABLE = "item";
inline constexpr std::string_view ITEM_VERSION_TABLE = "item_version";
inline constexpr std::string_view TAG_TABLE = "tag";
inline constexpr std::string_view RELATION_TABLE = "relation";
inline constexpr std::string_view DECISION_TABLE = "decision";

// 파일과 오가는 store 와 모델 표. SpecFile 이 세부 클래스에 빌려준다.
struct SpecTables {
    Failure& failure;
    const ModelCatalog& catalog;
    MetaStore& meta;
    CheckpointStore& checkpoints;
    ItemStore& items;
    RelationStore& relations;
    DecisionStore& decisions;
};

}
