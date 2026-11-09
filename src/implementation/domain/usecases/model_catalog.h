#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/domain/entity/model.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// 설계서 v1 3.3 ~ 3.5 의 모델 표. 코드가 정한 표를 load 에서 한 번 적재하며, 런타임에 바뀌지 않는다.
// Model 이 이 객체의 배열을 가리키므로 복사하지 않는다.
class ModelCatalog
{
public:
    static constexpr size_t TAG_COUNT = 13;
    static constexpr size_t RELATION_COUNT = 19;

    ModelCatalog() = default;
    ModelCatalog(const ModelCatalog&) = delete;
    ModelCatalog& operator=(const ModelCatalog&) = delete;

    KiottySpecStatus load();

    const entity::Model& model(entity::ItemKind kind) const { return _models[entity::kindIndex(kind)]; }

    Result<entity::ItemKind, KiottySpecStatus> findKindByPrefix(Span<const char> prefix) const;
    Result<uint32_t, KiottySpecStatus> findTag(entity::ItemKind kind, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findTagBit(entity::ItemKind kind, uint32_t tag, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findRelation(entity::ItemKind kind, Span<const char> name) const;

    // 태그 index 가 정의 수 이상이거나, 이름 없는 비트를 켜거나, SINGLE 태그에 비트 둘 이상을 켜면 INVALID_ARGUMENT.
    // value == 0 은 "레코드를 지운다"이므로 허용한다.
    KiottySpecStatus checkTagValue(entity::ItemKind kind, uint32_t tag, uint64_t value) const;

    // 관계 index 가 정의 수 이상이거나 목적어 Kind 가 정의의 target_kind 와 다르면 INVALID_ARGUMENT.
    KiottySpecStatus checkRelation(entity::ItemKind from, uint32_t relation, entity::ItemKind to) const;

private:
    entity::Model _models[entity::ITEM_KIND_COUNT] {};
    entity::TagDefinition _tags[TAG_COUNT] {};
    entity::RelationDefinition _relations[RELATION_COUNT] {};
};

}
