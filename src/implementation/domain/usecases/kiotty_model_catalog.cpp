#include "kiotty_model_catalog.h"

#include "implementation/core/kiotty_search.h"
#include "implementation/core/kiotty_text_view.h"

#include <iterator>
#include <string_view>

namespace kiotty::spec::implementation {

const std::string_view CHANGE_REQUEST_STATE_TAG {"상태"};
const std::string_view CHANGE_REQUEST_OPEN_BIT {"열림"};
const std::string_view CHANGE_REQUEST_IN_PROGRESS_BIT {"반영 중"};
const std::string_view CHANGE_REQUEST_ENTRY_STEP_TAG {"진입 단계"};
const std::string_view REQUIREMENT_KIND_TAG {"요구사항 종류"};
const std::string_view FUNCTIONAL_REQUIREMENT_BIT {"기능 요구사항"};
const std::string_view IMPLEMENTATION_STATE_TAG {"구현 상태"};
const std::string_view IMPLEMENTED_BIT {"구현 완료"};
const std::string_view NEEDS_CHANGE_BIT {"변경 필요"};
const std::string_view RESULT_TAG {"결과"};
const std::string_view PASS_BIT {"PASS"};
const std::string_view MILESTONE_DESIGN_RELATION {"포함 설계 사항"};
const std::string_view MILESTONE_UNIT_TEST_RELATION {"포함 유닛 테스트"};
const std::string_view SYSTEM_TEST_REQUIREMENT_RELATION {"검증 요구사항"};
const std::string_view CODE_INSPECTION_RULE_RELATION {"검사 개발 수칙"};
const std::string_view END_TO_END_TEST_INTERFACE_RELATION {"검증 외부 인터페이스"};
const std::string_view ACCEPTANCE_CRITERIA_KEYWORDS[ACCEPTANCE_CRITERIA_KEYWORD_COUNT] {
    "인수 조건", "인수조건"
};

namespace {

using entity::Cardinality;
using entity::ItemKind;
using entity::Step;

struct ModelSource
{
    const char* name {};
    const char* id_prefix {};
    ItemKind kind {};
    Step created_in {};
};

struct TagSource
{
    const char* bits[entity::TAG_BIT_COUNT] {};
    const char* name {};
    ItemKind kind {};
    Cardinality cardinality {};
};

struct RelationSource
{
    const char* name {};
    ItemKind kind {};
    ItemKind target_kind {};
    Cardinality cardinality {};
};

static_assert(sizeof(ModelSource) == 24, "unexpected padding");
static_assert(sizeof(TagSource) == 400, "unexpected padding");
static_assert(sizeof(RelationSource) == 24, "unexpected padding");

const ModelSource MODEL_SOURCES[] = {
    {"변경 요청", "CR", ItemKind::CHANGE_REQUEST, Step::CHANGE_REQUEST_REPORTED},
    {"요구사항", "REQ", ItemKind::REQUIREMENT, Step::REQUIREMENT_DISCUSSING},
    {"개발 수칙", "RULE", ItemKind::RULE, Step::REQUIREMENT_DISCUSSING},
    {"외부 인터페이스", "UI", ItemKind::EXTERNAL_INTERFACE, Step::EXTERNAL_INTERFACE_DISCUSSING},
    {"설계 사항", "DSN", ItemKind::DESIGN, Step::DESIGN_DISCUSSING},
    {"유닛 테스트", "UT", ItemKind::UNIT_TEST_PLAN, Step::UNIT_TEST_PLAN_DISCUSSING},
    {"마일스톤", "MS", ItemKind::MILESTONE, Step::DEVELOPMENT_PLAN_DISCUSSING},
    {"유닛 테스트 결과", "UTR", ItemKind::UNIT_TEST_RESULT, Step::IMPLEMENTING},
    {"시스템 테스트", "ST", ItemKind::SYSTEM_TEST_PLAN, Step::SYSTEM_TEST_PLAN_DISCUSSING},
    {"시스템 테스트 결과", "STR", ItemKind::SYSTEM_TEST_RESULT, Step::SYSTEM_TEST_RUN},
    {"코드 검사 결과", "CIR", ItemKind::CODE_INSPECTION_RESULT, Step::CODE_INSPECTING},
    {"E2E 테스트", "E2E", ItemKind::END_TO_END_TEST_PLAN, Step::END_TO_END_TEST_DISCUSSING},
    {"E2E 테스트 결과", "E2ER", ItemKind::END_TO_END_TEST_RESULT, Step::END_TO_END_TEST_RUN},
};

const TagSource TAG_SOURCES[] = {
    { { "사용자 피드백", "이슈", "초기 요구" },
      "출처",
      ItemKind::CHANGE_REQUEST,
      Cardinality::MANY },
    { { "열림", "반영 중", "완료", "거부" },
      "상태",
      ItemKind::CHANGE_REQUEST,
      Cardinality::SINGLE },
    { { "Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8", "Step 9",
        "Step 10", "Step 11", "Step 12" },
      "진입 단계",
      ItemKind::CHANGE_REQUEST,
      Cardinality::SINGLE },
    { { "기능 요구사항", "품질 요구사항", "환경 요구사항" },
      "요구사항 종류",
      ItemKind::REQUIREMENT,
      Cardinality::MANY },
    { { "아키텍처", "컨벤션", "코딩 제약", "의존성", "테스트" },
      "수칙 종류",
      ItemKind::RULE,
      Cardinality::MANY },
    { { "GUI 뷰", "C 함수", "C 타입" },
      "인터페이스 종류",
      ItemKind::EXTERNAL_INTERFACE,
      Cardinality::MANY },
    { { "모듈", "동작 흐름", "디렉토리", "메모리 정책" },
      "설계 종류",
      ItemKind::DESIGN,
      Cardinality::MANY },
    { { "미구현", "변경 필요", "구현 완료" },
      "구현 상태",
      ItemKind::DESIGN,
      Cardinality::SINGLE },
    { { "미구현", "변경 필요", "구현 완료" },
      "구현 상태",
      ItemKind::UNIT_TEST_PLAN,
      Cardinality::SINGLE },
    { { "PASS", "FAIL" },
      "결과",
      ItemKind::UNIT_TEST_RESULT,
      Cardinality::SINGLE },
    { { "PASS", "FAIL" },
      "결과",
      ItemKind::SYSTEM_TEST_RESULT,
      Cardinality::SINGLE },
    { { "PASS", "FAIL" },
      "결과",
      ItemKind::CODE_INSPECTION_RESULT,
      Cardinality::SINGLE },
    { { "PASS", "FAIL" },
      "결과",
      ItemKind::END_TO_END_TEST_RESULT,
      Cardinality::SINGLE },
};

const RelationSource RELATION_SOURCES[] = {
    { "영향 요구사항", ItemKind::CHANGE_REQUEST, ItemKind::REQUIREMENT, Cardinality::MANY },
    { "영향 개발 수칙", ItemKind::CHANGE_REQUEST, ItemKind::RULE, Cardinality::MANY },
    { "영향 외부 인터페이스", ItemKind::CHANGE_REQUEST, ItemKind::EXTERNAL_INTERFACE,
      Cardinality::MANY },
    { "영향 설계 사항", ItemKind::CHANGE_REQUEST, ItemKind::DESIGN, Cardinality::MANY },
    { "관련 마일스톤", ItemKind::CHANGE_REQUEST, ItemKind::MILESTONE, Cardinality::MANY },
    { "관련 요구사항", ItemKind::REQUIREMENT, ItemKind::REQUIREMENT, Cardinality::MANY },
    { "관련 요구사항", ItemKind::EXTERNAL_INTERFACE, ItemKind::REQUIREMENT, Cardinality::MANY },
    { "관련 요구사항", ItemKind::DESIGN, ItemKind::REQUIREMENT, Cardinality::MANY },
    { "관련 외부 인터페이스", ItemKind::DESIGN, ItemKind::EXTERNAL_INTERFACE, Cardinality::MANY },
    { "의존 설계 사항", ItemKind::DESIGN, ItemKind::DESIGN, Cardinality::MANY },
    { "검증 설계 사항", ItemKind::UNIT_TEST_PLAN, ItemKind::DESIGN, Cardinality::MANY },
    { "포함 설계 사항", ItemKind::MILESTONE, ItemKind::DESIGN, Cardinality::MANY },
    { "포함 유닛 테스트", ItemKind::MILESTONE, ItemKind::UNIT_TEST_PLAN, Cardinality::MANY },
    { "대상 유닛 테스트", ItemKind::UNIT_TEST_RESULT, ItemKind::UNIT_TEST_PLAN, Cardinality::MANY },
    { "검증 요구사항", ItemKind::SYSTEM_TEST_PLAN, ItemKind::REQUIREMENT, Cardinality::SINGLE },
    { "대상 시스템 테스트", ItemKind::SYSTEM_TEST_RESULT, ItemKind::SYSTEM_TEST_PLAN,
      Cardinality::MANY },
    { "검사 개발 수칙", ItemKind::CODE_INSPECTION_RESULT, ItemKind::RULE, Cardinality::SINGLE },
    { "검증 외부 인터페이스", ItemKind::END_TO_END_TEST_PLAN, ItemKind::EXTERNAL_INTERFACE,
      Cardinality::SINGLE },
    { "대상 E2E 테스트", ItemKind::END_TO_END_TEST_RESULT, ItemKind::END_TO_END_TEST_PLAN,
      Cardinality::MANY },
};

static_assert(std::size(MODEL_SOURCES) == entity::ITEM_KIND_COUNT, "one model per kind");
static_assert(std::size(TAG_SOURCES) == ModelCatalog::TAG_COUNT, "tag table size");
static_assert(std::size(RELATION_SOURCES) == ModelCatalog::RELATION_COUNT, "relation table size");

bool hasSingleBit(uint64_t value);
Result<entity::Model, KiottySpecStatus>
buildModel(const ModelSource& source, size_t index, Span<const entity::TagDefinition> tags,
           Span<const entity::RelationDefinition> relations);

}

KiottySpecStatus ModelCatalog::load()
{
    KiottySpecStatus status = loadTags();
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = loadRelations(); }
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT && status == KIOTTY_SPEC_STATUS_SUCCESS;
         ++index) {
        status = loadModel(index);
    }
    return status;
}

KiottySpecStatus ModelCatalog::loadTags()
{
    for (size_t index = 0; index < TAG_COUNT; ++index) {
        const TagSource& source = TAG_SOURCES[index];
        entity::TagDefinition& definition = _tags[index];
        KiottySpecStatus status = assignText(definition.name, source.name);
        for (size_t bit = 0; bit < entity::TAG_BIT_COUNT && status == KIOTTY_SPEC_STATUS_SUCCESS;
             ++bit) {
            status = assignText(definition.bit_names[bit], source.bits[bit]);
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        definition.cardinality = source.cardinality;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ModelCatalog::loadRelations()
{
    for (size_t index = 0; index < RELATION_COUNT; ++index) {
        const RelationSource& source = RELATION_SOURCES[index];
        entity::RelationDefinition& definition = _relations[index];
        const KiottySpecStatus status = assignText(definition.name, source.name);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        definition.target_kind = source.target_kind;
        definition.cardinality = source.cardinality;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ModelCatalog::loadModel(size_t index)
{
    const Span<const ModelSource> sources(MODEL_SOURCES, entity::ITEM_KIND_COUNT);
    const Span<entity::Model> models(_models, entity::ITEM_KIND_COUNT);
    const Span<const entity::TagDefinition> tags(_tags, TAG_COUNT);
    const Span<const entity::RelationDefinition> relations(_relations, RELATION_COUNT);
    const auto source = sources.at(index);
    const auto model = models.at(index);
    const bool has_slot = source && model;
    if (!has_slot) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const auto built = buildModel(*source.value, index, tags, relations);
    if (!built) { return built.code; }
    *model.value = built.value;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ModelCatalog::checkTagValue(ItemKind kind, uint32_t tag, uint64_t value) const
{
    const Span<const entity::TagDefinition> tags = model(kind).tags;
    const auto found = tags.at(tag);
    if (!found) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (value == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if ((value & ~entity::TAG_VALUE_MASK) != 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const entity::TagDefinition& definition = *found.value;
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        const bool is_set = (value & (uint64_t { 1 } << bit)) != 0;
        const bool is_unnamed = definition.bit_names[bit].empty();
        if (is_set && is_unnamed) {
            return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        }
    }
    const bool has_single_bit = hasSingleBit(value);
    const bool is_many = definition.cardinality != Cardinality::SINGLE;
    const bool can_hold_value = is_many || has_single_bit;
    if (can_hold_value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

KiottySpecStatus ModelCatalog::checkRelation(ItemKind from, uint32_t relation, ItemKind to) const
{
    const Span<const entity::RelationDefinition> relations = model(from).relations;
    const auto found = relations.at(relation);
    if (!found) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const entity::RelationDefinition& definition = *found.value;
    if (definition.target_kind != to) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

bool hasSingleBit(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

Result<entity::Model, KiottySpecStatus>
buildModel(const ModelSource& source, size_t index, Span<const entity::TagDefinition> tags,
           Span<const entity::RelationDefinition> relations)
{
    using ModelResult = Result<entity::Model, KiottySpecStatus>;
    const size_t kind_index = entity::kindIndex(source.kind);
    if (kind_index != index) { return ModelResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const auto ofKind = [&](const auto& entry) { return entry.kind == source.kind; };
    const Span<const TagSource> tag_sources(TAG_SOURCES, ModelCatalog::TAG_COUNT);
    const Span<const RelationSource> relation_sources(RELATION_SOURCES,
                                                      ModelCatalog::RELATION_COUNT);
    const Result<IndexRange, KiottySpecStatus> tag_range = contiguousRange(tag_sources, ofKind);
    const Result<IndexRange, KiottySpecStatus> relation_range =
        contiguousRange(relation_sources, ofKind);
    const auto first_tag = tags.at(tag_range.value.first);
    const auto first_relation = relations.at(relation_range.value.first);
    const bool has_ranges = tag_range && relation_range && first_tag && first_relation;
    if (!has_ranges) { return ModelResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    entity::Model model {};
    model.kind = source.kind;
    model.created_in = source.created_in;
    model.tags = Span<const entity::TagDefinition>(first_tag.value, tag_range.value.length);
    model.relations =
        Span<const entity::RelationDefinition>(first_relation.value, relation_range.value.length);
    KiottySpecStatus status = assignText(model.name, source.name);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = assignText(model.id_prefix, source.id_prefix);
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return ModelResult::failure(status); }
    return ModelResult::success(model);
}

}

}
