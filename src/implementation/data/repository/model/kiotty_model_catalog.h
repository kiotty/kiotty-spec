#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_model.h"
#include "implementation/data/repository/model/kiotty_model_finder.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

constexpr size_t ACCEPTANCE_CRITERIA_KEYWORD_COUNT = 2;

extern const std::string_view CHANGE_REQUEST_STATE_TAG;
extern const std::string_view CHANGE_REQUEST_OPEN_BIT;
extern const std::string_view CHANGE_REQUEST_IN_PROGRESS_BIT;
extern const std::string_view CHANGE_REQUEST_ENTRY_STEP_TAG;
extern const std::string_view REQUIREMENT_KIND_TAG;
extern const std::string_view FUNCTIONAL_REQUIREMENT_BIT;
extern const std::string_view IMPLEMENTATION_STATE_TAG;
extern const std::string_view IMPLEMENTED_BIT;
extern const std::string_view NEEDS_CHANGE_BIT;
extern const std::string_view RESULT_TAG;
extern const std::string_view PASS_BIT;
extern const std::string_view MILESTONE_DESIGN_RELATION;
extern const std::string_view MILESTONE_UNIT_TEST_RELATION;
extern const std::string_view SYSTEM_TEST_REQUIREMENT_RELATION;
extern const std::string_view CODE_INSPECTION_RULE_RELATION;
extern const std::string_view END_TO_END_TEST_INTERFACE_RELATION;
extern const std::string_view ACCEPTANCE_CRITERIA_KEYWORDS[ACCEPTANCE_CRITERIA_KEYWORD_COUNT];

class ModelCatalog
{
public:
    static constexpr size_t TAG_COUNT = 13;
    static constexpr size_t RELATION_COUNT = 19;
    static constexpr size_t BIT_NAME_COUNT = TAG_COUNT * entity::TAG_BIT_COUNT;

    ModelCatalog() = default;
    ModelCatalog(const ModelCatalog&) = delete;
    ModelCatalog& operator=(const ModelCatalog&) = delete;

    KiottySpecStatus load();

    const entity::Model& model(entity::ItemKind kind) const
    { return pickModel(Span<const entity::Model>(_models, entity::ITEM_KIND_COUNT), kind); }

    ModelFinder finder() const
    {
        return ModelFinder(ModelTables {
            Span<const entity::Model>(_models, entity::ITEM_KIND_COUNT),
            Span<const entity::TagDefinition>(_tags, TAG_COUNT),
            Span<const entity::RelationDefinition>(_relations, RELATION_COUNT),
            Span<const entity::TagName>(_bit_names, BIT_NAME_COUNT)});
    }

    KiottySpecStatus checkTagValue(entity::ItemKind kind, uint32_t tag, uint64_t value) const;

    KiottySpecStatus checkRelation(entity::ItemKind from, uint32_t relation,
                                   entity::ItemKind to) const;

private:
    KiottySpecStatus loadTags();
    KiottySpecStatus loadRelations();
    KiottySpecStatus loadModel(size_t index);

    entity::TagName _bit_names[BIT_NAME_COUNT] {};
    entity::Model _models[entity::ITEM_KIND_COUNT] {};
    entity::TagDefinition _tags[TAG_COUNT] {};
    entity::RelationDefinition _relations[RELATION_COUNT] {};
};

}
