#pragma once

#include "kiotty_item_records.h"
#include "kiotty_kind.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

// 설계서 v1 9.1
constexpr size_t MAX_MODEL_NAME_LENGTH = 31;
constexpr size_t MAX_TAG_NAME_LENGTH = 63;
constexpr size_t MAX_TAGS_PER_MODEL = 256;

using ModelName = FixedString<MAX_MODEL_NAME_LENGTH>;
using TagName = FixedString<MAX_TAG_NAME_LENGTH>;

// MANY 는 여러 비트 / 여러 대상, SINGLE 은 비트 하나 / 대상 하나 (1:1).
enum class Cardinality : uint32_t {
    MANY = 1,
    SINGLE
};

// 설계서 v1 3.4. bit_names 가 빈 칸인 비트는 켤 수 없다.
struct TagDefinition
{
    TagName name;
    TagName bit_names[TAG_BIT_COUNT];
    Cardinality cardinality;
};

// 설계서 v1 3.5
struct RelationDefinition
{
    TagName name;
    ItemKind target_kind;
    Cardinality cardinality;
};

static_assert(sizeof(TagDefinition) == 3140, "unexpected padding");
static_assert(sizeof(RelationDefinition) == 72, "unexpected padding");

// 설계서 v1 3.3. 정의 배열은 ModelCatalog 가 갖고, 모델은 그 일부를 가리킨다.
struct Model
{
    ItemKind kind;
    ModelName name;
    IdText id_prefix;
    Step created_in;
    Span<const TagDefinition> tags;
    Span<const RelationDefinition> relations;
};

}
