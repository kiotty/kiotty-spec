#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_model.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 모델 표에서 이름으로 Kind · 태그 · 태그 비트 · 관계의 index 를 찾는다. ModelCatalog 의 세부 클래스 (개발수칙 4.19).
class ModelFinder
{
public:
    explicit ModelFinder(Span<const entity::Model> models) : _models(models) {}

    Result<entity::ItemKind, KiottySpecStatus> findKindByPrefix(Span<const char> prefix) const;
    Result<uint32_t, KiottySpecStatus> findTag(entity::ItemKind kind, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findTagBit(entity::ItemKind kind, uint32_t tag, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findRelation(entity::ItemKind kind, Span<const char> name) const;

private:
    const entity::Model& model(entity::ItemKind kind) const { return _models[entity::kindIndex(kind)]; }

    Span<const entity::Model> _models;
};

}
