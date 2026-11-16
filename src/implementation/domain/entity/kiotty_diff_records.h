#pragma once

#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstdint>

namespace kiotty::spec::implementation::entity {

struct DiffRange {
    uint32_t from {0};
    uint32_t to {0};
};

static_assert(sizeof(DiffRange) == 8, "unexpected padding");

struct ItemDiffRow {
    IdText id {};
    uint32_t before {};
    uint32_t after {};
    uint32_t kind {};
    uint32_t change {};
    uint32_t fields {};
};

static_assert(sizeof(ItemDiffRow) == 36, "unexpected padding");

struct RelationDiffRow {
    IdText from_id {};
    IdText to_id {};
    uint32_t checkpoint {};
    uint32_t relation {};
    uint32_t model {};
    uint32_t change {};
};

static_assert(sizeof(RelationDiffRow) == 48, "unexpected padding");

struct KindCounts {
    uint32_t added {0};
    uint32_t modified {0};
    uint32_t removed {0};
    uint32_t relations_added {0};
    uint32_t relations_removed {0};
    bool has_decision_change {false};
};

static_assert(sizeof(KindCounts) == 24, "unexpected padding");

struct ItemVersionView {
    ItemLabel label {};
    ItemState state {};
    IdText parent_id {};
};

static_assert(sizeof(ItemVersionView) == 440, "unexpected padding");

struct RuleDiffRecord {
    TitleText title {};
    IdText id {};
    uint32_t change {0};
    uint32_t changed_fields {0};
};

static_assert(sizeof(RuleDiffRecord) == 152, "unexpected padding");

}
