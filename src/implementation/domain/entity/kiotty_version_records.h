#pragma once

#include "kiotty_item_records.h"
#include "kiotty_kind.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

// .kisp 파일의 한 줄이 레코드 한 버전이다 (docs/plan/261110_kisp_v1.md).
// 항목은 바뀌지 않는 key (8자리 16진수) 로 서로를 가리킨다. ID 는 item 줄에만 있다.
// removed 가 1 이면 "checkpoint 시점에 지워짐" 이라는 버전 (툼스톤) 이다.

constexpr uint32_t NO_KEY = 0;

// 공개 헤더의 KIOTTY_SPEC_MAX_DECISION_LENGTH 와 같다.
constexpr size_t MAX_DECISION_VERSION_LENGTH = 2048;
using DecisionVersionText = FixedString<MAX_DECISION_VERSION_LENGTH>;

struct ItemVersion
{
    using Identity = uint32_t;

    uint64_t body_hash {};
    uint32_t key {};
    uint32_t checkpoint {};
    uint32_t parent {};
    uint32_t removed {};
    ItemKind kind {};
    uint32_t _padding {};
    TitleText title {};
    SummaryText summary {};

    Identity identity() const { return key; }
};

struct TagIdentity
{
    uint32_t key {};
    uint32_t tag {};

    bool operator<(const TagIdentity& other) const { return key != other.key ? key < other.key : tag < other.tag; }
    bool operator==(const TagIdentity& other) const { return key == other.key && tag == other.tag; }
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

// "from 의 (relation) 은 to 다"
struct RelationIdentity
{
    uint32_t from {};
    uint32_t relation {};
    uint32_t to {};

    bool operator<(const RelationIdentity& other) const
    {
        if (from != other.from) { return from < other.from; }
        if (relation != other.relation) { return relation < other.relation; }
        return to < other.to;
    }
    bool operator==(const RelationIdentity& other) const { return from == other.from && relation == other.relation && to == other.to; }
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

    ItemKind kind {};
    uint32_t checkpoint {};
    uint32_t removed {};
    DecisionVersionText text {};

    Identity identity() const { return static_cast<uint32_t>(kind); }
};

static_assert(sizeof(ItemVersion) == 416, "unexpected padding");
static_assert(sizeof(TagVersion) == 24, "unexpected padding");
static_assert(sizeof(RelationVersion) == 24, "unexpected padding");
static_assert(sizeof(DecisionVersion) == 2064, "unexpected padding");

}
