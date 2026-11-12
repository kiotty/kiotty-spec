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
constexpr size_t MAX_DESCRIPTION_LENGTH = 63;
constexpr size_t TAG_BIT_COUNT = 48;
constexpr uint64_t TAG_VALUE_MASK = (uint64_t{1} << TAG_BIT_COUNT) - 1;

using IdText = FixedString<MAX_ID_LENGTH>;
using TitleText = FixedString<MAX_TITLE_LENGTH>;
using SummaryText = FixedString<MAX_SUMMARY_LENGTH>;
using DescriptionText = FixedString<MAX_DESCRIPTION_LENGTH>;

enum class HandleType : uint16_t {
    ITEM = 1,
    CURSOR,
    ITEM_KEY,
    ITEM_LABEL,
    DECISION_TEXT
};

using ItemHandle = TypedHandle<static_cast<uint16_t>(HandleType::ITEM)>;
using CursorHandle = TypedHandle<static_cast<uint16_t>(HandleType::CURSOR)>;

struct CheckpointHandle
{
    uint32_t sequence {};
    uint32_t reserved {};

    bool isValid() const { return sequence != 0; }
};

constexpr ItemHandle INVALID_ITEM_HANDLE {};
constexpr CheckpointHandle INVALID_CHECKPOINT_HANDLE {0, 0};

struct ItemState
{
    CheckpointHandle checkpoint {};
    ItemHandle parent {};
    ItemKind kind {};
    uint32_t _padding {};
};

struct ItemLabel
{
    SummaryText summary {};
    TitleText title {};
    IdText id {};
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
    int64_t created_at {};
    CheckpointHandle previous {};
    Step step {};
    CheckpointState state {};
};

static_assert(sizeof(ItemHandle) == 8, "unexpected padding");
static_assert(sizeof(CheckpointHandle) == 8, "unexpected padding");
static_assert(sizeof(ItemState) == 24, "unexpected padding");
static_assert(sizeof(ItemLabel) == 400, "unexpected padding");
static_assert(sizeof(TagRecord) == 24, "unexpected padding");
static_assert(sizeof(RelationRecord) == 24, "unexpected padding");
static_assert(sizeof(CheckpointRecord) == 24, "unexpected padding");

}
