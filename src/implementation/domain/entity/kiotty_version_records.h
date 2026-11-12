#pragma once

#include "kiotty_item_records.h"
#include "kiotty_kind.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

constexpr uint32_t NO_KEY = 0;

constexpr size_t MAX_DECISION_VERSION_LENGTH = 2048;
using DecisionVersionText = FixedString<MAX_DECISION_VERSION_LENGTH>;

using ItemKey = TypedHandle<static_cast<uint16_t>(HandleType::ITEM_KEY)>;
using ItemLabelHandle = TypedHandle<static_cast<uint16_t>(HandleType::ITEM_LABEL)>;
using DecisionTextHandle = TypedHandle<static_cast<uint16_t>(HandleType::DECISION_TEXT)>;

constexpr ItemKey makeItemKey(uint32_t key) { return ItemKey {key, 0}; }

struct ItemVersionLabel
{
    SummaryText summary {};
    TitleText title {};
};

struct ItemVersion
{
    using Identity = uint32_t;

    uint64_t body_hash {};
    ItemKey parent {};
    ItemLabelHandle label {};
    uint32_t key {};
    uint32_t checkpoint {};
    uint32_t removed {};
    ItemKind kind {};

    Identity identity() const { return key; }
};

struct TagIdentity
{
    uint32_t key {};
    uint32_t tag {};

    bool operator<(TagIdentity other) const
    { return key != other.key ? key < other.key : tag < other.tag; }
    bool operator==(TagIdentity other) const { return key == other.key && tag == other.tag; }
};

struct TagVersion
{
    using Identity = TagIdentity;

    uint64_t value {};
    ItemKey item {};
    uint32_t tag {};
    uint32_t checkpoint {};
    uint32_t removed {};
    uint32_t _padding {};

    Identity identity() const { return TagIdentity{item.index, tag}; }
};

struct RelationIdentity
{
    uint32_t from {};
    uint32_t relation {};
    uint32_t to {};

    bool operator<(RelationIdentity other) const
    {
        if (from != other.from) { return from < other.from; }
        if (relation != other.relation) { return relation < other.relation; }
        return to < other.to;
    }
    bool operator==(RelationIdentity other) const
    { return from == other.from && relation == other.relation && to == other.to; }
};

struct RelationVersion
{
    using Identity = RelationIdentity;

    ItemKey from {};
    ItemKey to {};
    uint32_t relation {};
    uint32_t checkpoint {};
    uint32_t removed {};
    ItemKind model {};

    Identity identity() const { return RelationIdentity{from.index, relation, to.index}; }
};

struct DecisionVersion
{
    using Identity = uint32_t;

    DecisionTextHandle text {};
    ItemKind kind {};
    uint32_t checkpoint {};
    uint32_t removed {};

    Identity identity() const { return static_cast<uint32_t>(kind); }
};

static_assert(sizeof(ItemVersionLabel) == 384, "unexpected padding");
static_assert(sizeof(ItemVersion) == 40, "unexpected padding");
static_assert(sizeof(TagIdentity) == 8, "unexpected padding");
static_assert(sizeof(RelationIdentity) == 12, "unexpected padding");
static_assert(sizeof(TagVersion) == 32, "unexpected padding");
static_assert(sizeof(RelationVersion) == 32, "unexpected padding");
static_assert(sizeof(DecisionVersion) == 20, "unexpected padding");

}
