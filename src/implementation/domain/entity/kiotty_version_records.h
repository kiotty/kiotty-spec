#pragma once

#include "kiotty_item_records.h"
#include "kiotty_kind.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

constexpr uint32_t NO_KEY = 0;

constexpr size_t MAX_DECISION_VERSION_LENGTH = 2048;
using DecisionVersionText = FixedString<MAX_DECISION_VERSION_LENGTH>;

struct ItemVersion
{
    using Identity = uint32_t;

    SummaryText summary {};
    TitleText title {};
    uint64_t body_hash {};
    uint32_t key {};
    uint32_t checkpoint {};
    uint32_t parent {};
    uint32_t removed {};
    ItemKind kind {};
    uint32_t _padding {};

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
    uint32_t key {};
    uint32_t tag {};
    uint32_t checkpoint {};
    uint32_t removed {};

    Identity identity() const { return TagIdentity{key, tag}; }
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

    uint32_t from {};
    uint32_t relation {};
    uint32_t to {};
    uint32_t checkpoint {};
    uint32_t removed {};
    ItemKind model {};

    Identity identity() const { return RelationIdentity{from, relation, to}; }
};

struct DecisionVersion
{
    using Identity = uint32_t;

    DecisionVersionText text {};
    ItemKind kind {};
    uint32_t checkpoint {};
    uint32_t removed {};

    Identity identity() const { return static_cast<uint32_t>(kind); }
};

static_assert(sizeof(ItemVersion) == 416, "unexpected padding");
static_assert(sizeof(TagIdentity) == 8, "unexpected padding");
static_assert(sizeof(RelationIdentity) == 12, "unexpected padding");
static_assert(sizeof(TagVersion) == 24, "unexpected padding");
static_assert(sizeof(RelationVersion) == 24, "unexpected padding");
static_assert(sizeof(DecisionVersion) == 2064, "unexpected padding");

}
