#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstdint>

namespace kiotty::spec::implementation::entity {

struct TagQuery {
    uint64_t mask {};
    ItemKind kind {};
    uint32_t tag {};
};

static_assert(sizeof(TagQuery) == 16, "unexpected padding");

struct Position {
    uint64_t pending_writes {0};
    uint64_t open_change_requests {0};
    uint32_t current {0};
    uint32_t step {0};
    uint32_t state {0};
    uint32_t next_step {0};
};

static_assert(sizeof(Position) == 32, "unexpected padding");

struct UnmetCondition {
    IdText id {};
    KiottySpecCondition condition {KIOTTY_SPEC_CONDITION_ENTRY_STEP};
};

static_assert(sizeof(UnmetCondition) == 20, "unexpected padding");

struct ChangeRequestSummary {
    uint64_t open_count {0};
    uint32_t earliest_entry_step {0};
};

static_assert(sizeof(ChangeRequestSummary) == 16, "unexpected padding");

}
