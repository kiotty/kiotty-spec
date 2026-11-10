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
const std::string_view ACCEPTANCE_CRITERIA_KEYWORDS[ACCEPTANCE_CRITERIA_KEYWORD_COUNT] {"인수 조건", "인수조건"};

namespace {

using entity::Cardinality;
using entity::ItemKind;
using entity::Step;

struct ModelSource
{
    ItemKind kind;
    const char* name;
    const char* id_prefix;
    Step created_in;
};

struct TagSource
{
    ItemKind kind;
    const char* name;
    Cardinality cardinality;
    const char* bits[entity::TAG_BIT_COUNT];
};

struct RelationSource
{
    ItemKind kind;
    const char* name;
    ItemKind target_kind;
    Cardinality cardinality;
};

// 설계서 v1 3.1. Kind 값 순서다.
const ModelSource MODEL_SOURCES[] = {
    {ItemKind::CHANGE_REQUEST, "변경 요청", "CR", Step::CHANGE_REQUEST_REPORTED},
    {ItemKind::REQUIREMENT, "요구사항", "REQ", Step::REQUIREMENT_DISCUSSING},
    {ItemKind::RULE, "개발 수칙", "RULE", Step::REQUIREMENT_DISCUSSING},
    {ItemKind::EXTERNAL_INTERFACE, "외부 인터페이스", "UI", Step::EXTERNAL_INTERFACE_DISCUSSING},
    {ItemKind::DESIGN, "설계 사항", "DSN", Step::DESIGN_DISCUSSING},
    {ItemKind::UNIT_TEST_PLAN, "유닛 테스트", "UT", Step::UNIT_TEST_PLAN_DISCUSSING},
    {ItemKind::MILESTONE, "마일스톤", "MS", Step::DEVELOPMENT_PLAN_DISCUSSING},
    {ItemKind::UNIT_TEST_RESULT, "유닛 테스트 결과", "UTR", Step::IMPLEMENTING},
    {ItemKind::SYSTEM_TEST_PLAN, "시스템 테스트", "ST", Step::SYSTEM_TEST_PLAN_DISCUSSING},
    {ItemKind::SYSTEM_TEST_RESULT, "시스템 테스트 결과", "STR", Step::SYSTEM_TEST_RUN},
    {ItemKind::CODE_INSPECTION_RESULT, "코드 검사 결과", "CIR", Step::CODE_INSPECTING},
    {ItemKind::END_TO_END_TEST_PLAN, "E2E 테스트", "E2E", Step::END_TO_END_TEST_DISCUSSING},
    {ItemKind::END_TO_END_TEST_RESULT, "E2E 테스트 결과", "E2ER", Step::END_TO_END_TEST_RUN},
};

// 설계서 v1 3.4. 같은 Kind 의 태그는 이어서 적고, Kind 안의 순서가 태그 index 다.
const TagSource TAG_SOURCES[] = {
    {ItemKind::CHANGE_REQUEST, "출처", Cardinality::MANY, {"사용자 피드백", "이슈", "초기 요구"}},
    {ItemKind::CHANGE_REQUEST, "상태", Cardinality::SINGLE, {"열림", "반영 중", "완료", "거부"}},
    {ItemKind::CHANGE_REQUEST, "진입 단계", Cardinality::SINGLE,
     {"Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8", "Step 9", "Step 10", "Step 11", "Step 12"}},
    {ItemKind::REQUIREMENT, "요구사항 종류", Cardinality::MANY, {"기능 요구사항", "품질 요구사항", "환경 요구사항"}},
    {ItemKind::RULE, "수칙 종류", Cardinality::MANY, {"아키텍처", "컨벤션", "코딩 제약", "의존성", "테스트"}},
    {ItemKind::EXTERNAL_INTERFACE, "인터페이스 종류", Cardinality::MANY, {"GUI 뷰", "C 함수", "C 타입"}},
    {ItemKind::DESIGN, "설계 종류", Cardinality::MANY, {"모듈", "동작 흐름", "디렉토리", "메모리 정책"}},
    {ItemKind::DESIGN, "구현 상태", Cardinality::SINGLE, {"미구현", "변경 필요", "구현 완료"}},
    {ItemKind::UNIT_TEST_PLAN, "구현 상태", Cardinality::SINGLE, {"미구현", "변경 필요", "구현 완료"}},
    {ItemKind::UNIT_TEST_RESULT, "결과", Cardinality::SINGLE, {"PASS", "FAIL"}},
    {ItemKind::SYSTEM_TEST_RESULT, "결과", Cardinality::SINGLE, {"PASS", "FAIL"}},
    {ItemKind::CODE_INSPECTION_RESULT, "결과", Cardinality::SINGLE, {"PASS", "FAIL"}},
    {ItemKind::END_TO_END_TEST_RESULT, "결과", Cardinality::SINGLE, {"PASS", "FAIL"}},
};

// 설계서 v1 3.5. 같은 Kind 의 관계는 이어서 적고, Kind 안의 순서가 관계 index 다.
const RelationSource RELATION_SOURCES[] = {
    {ItemKind::CHANGE_REQUEST, "영향 요구사항", ItemKind::REQUIREMENT, Cardinality::MANY},
    {ItemKind::CHANGE_REQUEST, "영향 개발 수칙", ItemKind::RULE, Cardinality::MANY},
    {ItemKind::CHANGE_REQUEST, "영향 외부 인터페이스", ItemKind::EXTERNAL_INTERFACE, Cardinality::MANY},
    {ItemKind::CHANGE_REQUEST, "영향 설계 사항", ItemKind::DESIGN, Cardinality::MANY},
    {ItemKind::CHANGE_REQUEST, "관련 마일스톤", ItemKind::MILESTONE, Cardinality::MANY},
    {ItemKind::REQUIREMENT, "관련 요구사항", ItemKind::REQUIREMENT, Cardinality::MANY},
    {ItemKind::EXTERNAL_INTERFACE, "관련 요구사항", ItemKind::REQUIREMENT, Cardinality::MANY},
    {ItemKind::DESIGN, "관련 요구사항", ItemKind::REQUIREMENT, Cardinality::MANY},
    {ItemKind::DESIGN, "관련 외부 인터페이스", ItemKind::EXTERNAL_INTERFACE, Cardinality::MANY},
    {ItemKind::DESIGN, "의존 설계 사항", ItemKind::DESIGN, Cardinality::MANY},
    {ItemKind::UNIT_TEST_PLAN, "검증 설계 사항", ItemKind::DESIGN, Cardinality::MANY},
    {ItemKind::MILESTONE, "포함 설계 사항", ItemKind::DESIGN, Cardinality::MANY},
    {ItemKind::MILESTONE, "포함 유닛 테스트", ItemKind::UNIT_TEST_PLAN, Cardinality::MANY},
    {ItemKind::UNIT_TEST_RESULT, "대상 유닛 테스트", ItemKind::UNIT_TEST_PLAN, Cardinality::MANY},
    {ItemKind::SYSTEM_TEST_PLAN, "검증 요구사항", ItemKind::REQUIREMENT, Cardinality::SINGLE},
    {ItemKind::SYSTEM_TEST_RESULT, "대상 시스템 테스트", ItemKind::SYSTEM_TEST_PLAN, Cardinality::MANY},
    {ItemKind::CODE_INSPECTION_RESULT, "검사 개발 수칙", ItemKind::RULE, Cardinality::SINGLE},
    {ItemKind::END_TO_END_TEST_PLAN, "검증 외부 인터페이스", ItemKind::EXTERNAL_INTERFACE, Cardinality::SINGLE},
    {ItemKind::END_TO_END_TEST_RESULT, "대상 E2E 테스트", ItemKind::END_TO_END_TEST_PLAN, Cardinality::MANY},
};

static_assert(std::size(MODEL_SOURCES) == entity::ITEM_KIND_COUNT, "one model per kind");
static_assert(std::size(TAG_SOURCES) == ModelCatalog::TAG_COUNT, "tag table size");
static_assert(std::size(RELATION_SOURCES) == ModelCatalog::RELATION_COUNT, "relation table size");

bool hasSingleBit(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

}

KiottySpecStatus ModelCatalog::load()
{
    KiottySpecStatus status = loadTags();
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = loadRelations(); }
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) { status = loadModel(index); }
    return status;
}

KiottySpecStatus ModelCatalog::loadTags()
{
    for (size_t index = 0; index < TAG_COUNT; ++index) {
        const TagSource& source = TAG_SOURCES[index];
        entity::TagDefinition& definition = _tags[index];
        KiottySpecStatus status = assignText(definition.name, source.name);
        for (size_t bit = 0; bit < entity::TAG_BIT_COUNT && status == KIOTTY_SPEC_STATUS_SUCCESS; ++bit) {
            definition.bit_names[bit].clear();
            if (source.bits[bit] != nullptr) { status = assignText(definition.bit_names[bit], source.bits[bit]); }
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

// 태그 · 관계 표에서 이 Kind 의 정의가 이어진 구간을 모델에 건다. 이어지지 않으면 표가 잘못된 것이다.
KiottySpecStatus ModelCatalog::loadModel(size_t index)
{
    const ModelSource& source = MODEL_SOURCES[index];
    entity::Model& model = _models[index];
    if (entity::kindIndex(source.kind) != index) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    model.kind = source.kind;
    model.created_in = source.created_in;
    const auto ofKind = [&](const auto& entry) { return entry.kind == source.kind; };
    const Result<IndexRange, KiottySpecStatus> tags = contiguousRange(Span<const TagSource>(TAG_SOURCES, TAG_COUNT), ofKind);
    const Result<IndexRange, KiottySpecStatus> relations = contiguousRange(Span<const RelationSource>(RELATION_SOURCES, RELATION_COUNT), ofKind);
    if (!tags || !relations) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    model.tags = Span<const entity::TagDefinition>(_tags + tags.value.first, tags.value.length);
    model.relations = Span<const entity::RelationDefinition>(_relations + relations.value.first, relations.value.length);
    KiottySpecStatus status = assignText(model.name, source.name);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = assignText(model.id_prefix, source.id_prefix); }
    return status;
}

KiottySpecStatus ModelCatalog::checkTagValue(ItemKind kind, uint32_t tag, uint64_t value) const
{
    const Span<const entity::TagDefinition> tags = model(kind).tags;
    if (tag >= tags.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (value == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if ((value & ~entity::TAG_VALUE_MASK) != 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const entity::TagDefinition& definition = tags[tag];
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) != 0 && definition.bit_names[bit].empty()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    }
    if (definition.cardinality == Cardinality::SINGLE && !hasSingleBit(value)) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ModelCatalog::checkRelation(ItemKind from, uint32_t relation, ItemKind to) const
{
    const Span<const entity::RelationDefinition> relations = model(from).relations;
    if (relation >= relations.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (relations[relation].target_kind != to) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
