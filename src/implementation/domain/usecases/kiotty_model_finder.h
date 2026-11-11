#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_model.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class ModelFinder
{
public:
    explicit ModelFinder(Span<const entity::Model> models) : _models(models) {}

    Result<uint32_t, KiottySpecStatus> findTag(entity::ItemKind kind, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findTagBit(entity::ItemKind kind, uint32_t tag, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findRelation(entity::ItemKind kind, Span<const char> name) const;

private:
    const entity::Model& model(entity::ItemKind kind) const { return _models[entity::kindIndex(kind)]; }

    Span<const entity::Model> _models;
};

}
