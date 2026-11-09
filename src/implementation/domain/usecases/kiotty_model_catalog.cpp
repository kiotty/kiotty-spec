#include "kiotty_model_catalog.h"

#include "implementation/core/kiotty_text_view.h"

#include <iterator>
#include <string_view>

namespace kiotty::spec::implementation {

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

template <size_t Capacity>
KiottySpecStatus assignText(FixedString<Capacity>& target, const char* text)
{
    return target.assign(boundedText(text, Capacity + 1));
}

// 표에서 kind 의 정의가 이어진 구간을 찾는다. 이어지지 않으면 표가 잘못된 것이다.
template <typename Definition, typename Source, size_t Count>
KiottySpecStatus findRange(ItemKind kind, const Source (&sources)[Count], const Definition* definitions, Span<const Definition>& out)
{
    size_t first = Count;
    size_t length = 0;
    for (size_t index = 0; index < Count; ++index) {
        if (sources[index].kind != kind) { continue; }
        if (first == Count) { first = index; }
        if (index != first + length) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        ++length;
    }
    out = length == 0 ? Span<const Definition>() : Span<const Definition>(definitions + first, length);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

bool hasSingleBit(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

}

KiottySpecStatus ModelCatalog::load()
{
    for (size_t index = 0; index < TAG_COUNT; ++index) {
        const TagSource& source = TAG_SOURCES[index];
        entity::TagDefinition& definition = _tags[index];
        if (const KiottySpecStatus status = assignText(definition.name, source.name); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
            definition.bit_names[bit].clear();
            if (source.bits[bit] == nullptr) { continue; }
            if (const KiottySpecStatus status = assignText(definition.bit_names[bit], source.bits[bit]); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        }
        definition.cardinality = source.cardinality;
    }
    for (size_t index = 0; index < RELATION_COUNT; ++index) {
        const RelationSource& source = RELATION_SOURCES[index];
        entity::RelationDefinition& definition = _relations[index];
        if (const KiottySpecStatus status = assignText(definition.name, source.name); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        definition.target_kind = source.target_kind;
        definition.cardinality = source.cardinality;
    }
    for (size_t index = 0; index < entity::ITEM_KIND_COUNT; ++index) {
        const ModelSource& source = MODEL_SOURCES[index];
        entity::Model& model = _models[index];
        if (entity::kindIndex(source.kind) != index) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        model.kind = source.kind;
        model.created_in = source.created_in;
        if (const KiottySpecStatus status = assignText(model.name, source.name); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        if (const KiottySpecStatus status = assignText(model.id_prefix, source.id_prefix); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        if (const KiottySpecStatus status = findRange(source.kind, TAG_SOURCES, _tags, model.tags); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        if (const KiottySpecStatus status = findRange(source.kind, RELATION_SOURCES, _relations, model.relations); status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
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
