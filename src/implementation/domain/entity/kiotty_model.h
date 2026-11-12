#pragma once

#include "kiotty_item_records.h"
#include "kiotty_kind.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_search.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

constexpr size_t MAX_MODEL_NAME_LENGTH = 31;
constexpr size_t MAX_TAG_NAME_LENGTH = 63;
constexpr size_t MAX_TAGS_PER_MODEL = 256;

using ModelName = FixedString<MAX_MODEL_NAME_LENGTH>;
using TagName = FixedString<MAX_TAG_NAME_LENGTH>;

enum class Cardinality : uint32_t {
    MANY = 1,
    SINGLE
};

struct TagDefinition
{
    TagName name {};
    Cardinality cardinality {};
};

struct RelationDefinition
{
    TagName name {};
    ItemKind target_kind {};
    Cardinality cardinality {};
};

static_assert(sizeof(TagDefinition) == 68, "unexpected padding");
static_assert(sizeof(RelationDefinition) == 72, "unexpected padding");

struct Model
{
    ModelName name {};
    IdText id_prefix {};
    IndexRange tags {};
    IndexRange relations {};
    ItemKind kind {};
    Step created_in {};
};

static_assert(sizeof(Model) == 88, "unexpected padding");

}
