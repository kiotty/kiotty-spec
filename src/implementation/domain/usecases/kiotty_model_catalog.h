#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_model.h"
#include "implementation/domain/usecases/kiotty_model_finder.h"

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

    // 이름으로 찾기는 세부 클래스에 맡긴다 (개발수칙 4.19). 부른 자리에서 쓰고 버린다.
    ModelFinder finder() const { return ModelFinder(Span<const entity::Model>(_models, entity::ITEM_KIND_COUNT)); }

    // 태그 index 가 정의 수 이상이거나, 이름 없는 비트를 켜거나, SINGLE 태그에 비트 둘 이상을 켜면 INVALID_ARGUMENT.
    // value == 0 은 "레코드를 지운다"이므로 허용한다.
    KiottySpecStatus checkTagValue(entity::ItemKind kind, uint32_t tag, uint64_t value) const;

    // 관계 index 가 정의 수 이상이거나 목적어 Kind 가 정의의 target_kind 와 다르면 INVALID_ARGUMENT.
    KiottySpecStatus checkRelation(entity::ItemKind from, uint32_t relation, entity::ItemKind to) const;

private:
    KiottySpecStatus loadTags();
    KiottySpecStatus loadRelations();
    KiottySpecStatus loadModel(size_t index);

    entity::Model _models[entity::ITEM_KIND_COUNT] {};
    entity::TagDefinition _tags[TAG_COUNT] {};
    entity::RelationDefinition _relations[RELATION_COUNT] {};
};

}
