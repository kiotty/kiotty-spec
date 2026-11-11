#pragma once

#include "kiotty_kind.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_handle.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

// 설계서 v1 9.1 용량 상수
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

// 상위 4바이트 generation, 하위 4바이트 index (설계서 v1 4)
using ItemHandle = Handle;

// 상위 4바이트 reserved(0), 하위 4바이트 sequence. sequence == 0 이 무효 값이다.
struct CheckpointHandle
{
    uint32_t sequence {};
    uint32_t reserved {};

    bool isValid() const { return sequence != 0; }
};

constexpr ItemHandle INVALID_ITEM_HANDLE {Handle::INVALID_INDEX, 0};
constexpr CheckpointHandle INVALID_CHECKPOINT_HANDLE {0, 0};

// 설계서 v1 3.2. 멤버는 크기 내림차순이다.
struct ItemRecord
{
    CheckpointHandle checkpoint {};
    ItemHandle parent {};
    ItemKind kind {};
    uint32_t _padding {};
    IdText id {};
    TitleText title {};
    SummaryText summary {};
};

// 설계서 v1 3.6. (item, tag) 쌍마다 하나이며 value 는 하위 48비트만 쓴다.
struct TagRecord
{
    ItemHandle item {};
    ItemKind model {};
    uint32_t tag {};
    uint64_t value {};
};

// 설계서 v1 3.5. "from 의 (relation) 은 to 다".
struct RelationRecord
{
    ItemHandle from {};
    ItemHandle to {};
    ItemKind model {};
    uint32_t relation {};
};

// 설계서 v1 5.1
struct CheckpointRecord
{
    int64_t created_at {};
    CheckpointHandle previous {};
    Step step {};
    CheckpointState state {};
    DescriptionText description {};
};

// 설계서 v1 6
struct DecisionRecord
{
    CheckpointHandle checkpoint {};
    ItemKind kind {};
    uint32_t _padding {};
    DecisionText description {};
};

static_assert(sizeof(ItemHandle) == 8, "unexpected padding");
static_assert(sizeof(CheckpointHandle) == 8, "unexpected padding");
static_assert(sizeof(ItemRecord) == 424, "unexpected padding");
static_assert(sizeof(TagRecord) == 24, "unexpected padding");
static_assert(sizeof(RelationRecord) == 24, "unexpected padding");
static_assert(sizeof(CheckpointRecord) == 88, "unexpected padding");
static_assert(sizeof(DecisionRecord) == 2064, "unexpected padding");

}
