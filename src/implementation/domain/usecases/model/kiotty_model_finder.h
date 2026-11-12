#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_model.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct ModelTables {
    Span<const entity::Model> models {};
    Span<const entity::TagDefinition> tags {};
    Span<const entity::RelationDefinition> relations {};
    Span<const entity::TagName> bit_names {};
};

static_assert(sizeof(ModelTables) == 64, "unexpected padding");

const entity::Model& pickModel(Span<const entity::Model> models, entity::ItemKind kind);

class ModelFinder
{
public:
    explicit ModelFinder(const ModelTables& tables) : _tables(tables) {}

    Result<uint32_t, KiottySpecStatus> findTag(entity::ItemKind kind, Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findTagBit(entity::ItemKind kind, uint32_t tag,
                                                  Span<const char> name) const;
    Result<uint32_t, KiottySpecStatus> findRelation(entity::ItemKind kind,
                                                    Span<const char> name) const;
    Span<const entity::TagDefinition> tagsOf(entity::ItemKind kind) const;
    Span<const entity::RelationDefinition> relationsOf(entity::ItemKind kind) const;
    Span<const entity::TagName> bitNamesOf(entity::ItemKind kind, uint32_t tag) const;

private:
    const entity::Model& model(entity::ItemKind kind) const
    { return pickModel(_tables.models, kind); }

    ModelTables _tables;
};

}
