#pragma once

#include "kiotty_kind.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_handle.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

constexpr size_t MAX_ID_LENGTH = 15;
constexpr size_t MAX_TITLE_LENGTH = 127;
constexpr size_t MAX_SUMMARY_LENGTH = 255;
constexpr size_t MAX_BODY_LENGTH = 16384;
constexpr size_t MAX_DECISION_LENGTH = 2047;
constexpr size_t MAX_DESCRIPTION_LENGTH = 63;
constexpr size_t TAG_BIT_COUNT = 48;
constexpr uint64_t TAG_VALUE_MASK = (uint64_t{1} << TAG_BIT_COUNT) - 1;

using IdText = FixedString<MAX_ID_LENGTH>;
using TitleText = FixedString<MAX_TITLE_LENGTH>;
using SummaryText = FixedString<MAX_SUMMARY_LENGTH>;
using DescriptionText = FixedString<MAX_DESCRIPTION_LENGTH>;
using DecisionText = FixedString<MAX_DECISION_LENGTH>;

using ItemHandle = Handle;

struct CheckpointHandle
{
    uint32_t sequence {};
    uint32_t reserved {};

    bool isValid() const { return sequence != 0; }
};

constexpr ItemHandle INVALID_ITEM_HANDLE {Handle::INVALID_INDEX, 0};
constexpr CheckpointHandle INVALID_CHECKPOINT_HANDLE {0, 0};

struct ItemRecord
{
    SummaryText summary {};
    TitleText title {};
    IdText id {};
    CheckpointHandle checkpoint {};
    ItemHandle parent {};
    ItemKind kind {};
    uint32_t _padding {};
};

struct TagRecord
{
    uint64_t value {};
    ItemHandle item {};
    ItemKind model {};
    uint32_t tag {};
};

struct RelationRecord
{
    ItemHandle from {};
    ItemHandle to {};
    ItemKind model {};
    uint32_t relation {};
};

struct CheckpointRecord
{
    DescriptionText description {};
    int64_t created_at {};
    CheckpointHandle previous {};
    Step step {};
    CheckpointState state {};
};

struct DecisionRecord
{
    DecisionText description {};
    CheckpointHandle checkpoint {};
    ItemKind kind {};
    uint32_t _padding {};
};

static_assert(sizeof(ItemHandle) == 8, "unexpected padding");
static_assert(sizeof(CheckpointHandle) == 8, "unexpected padding");
static_assert(sizeof(ItemRecord) == 424, "unexpected padding");
static_assert(sizeof(TagRecord) == 24, "unexpected padding");
static_assert(sizeof(RelationRecord) == 24, "unexpected padding");
static_assert(sizeof(CheckpointRecord) == 88, "unexpected padding");
static_assert(sizeof(DecisionRecord) == 2064, "unexpected padding");

}
