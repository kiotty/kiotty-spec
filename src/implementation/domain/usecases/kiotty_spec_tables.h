#pragma once

#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/data/repository/kiotty_checkpoint_store.h"
#include "implementation/data/repository/kiotty_decision_store.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_meta_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/usecases/kiotty_model_catalog.h"

#include <string_view>

namespace kiotty::spec::implementation {

inline constexpr std::string_view KISP_HEADER = "#kisp 2 model 1";
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
inline constexpr std::string_view BODY_TABLE = "body";

struct SpecTables {
    entity::Failure& failure;
    const ModelCatalog& catalog;
    MetaStore& meta;
    CheckpointStore& checkpoints;
    ItemStore& items;
    RelationStore& relations;
    DecisionStore& decisions;
};

static_assert(sizeof(SpecTables) == 56, "unexpected padding");

}
