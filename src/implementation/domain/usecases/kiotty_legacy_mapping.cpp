#include "implementation/domain/usecases/kiotty_legacy_mapping.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr size_t MAX_NAME_LENGTH = 64;

struct LegacyKind {
    std::string_view legacy;
    std::string_view kind;
};

constexpr LegacyKind LEGACY_KINDS[] = {
    {"requirement", "requirement"},  {"rule", "rule"},     {"interface", "external_interface"}, {"design", "design"},
    {"unit_test", "unit_test_plan"}, {"system_test", "system_test_plan"}, {"e2e_test", "e2e_test_plan"},
    {"milestone", "milestone"},      {"plan", "milestone"},
};

struct LegacyCategory {
    std::string_view category;
    const char* tag;
    const char* bit;
};

constexpr LegacyCategory LEGACY_CATEGORIES[] = {
    {"functional", "요구사항 종류", "기능 요구사항"}, {"quality", "요구사항 종류", "품질 요구사항"},
    {"environment", "요구사항 종류", "환경 요구사항"}, {"architecture", "수칙 종류", "아키텍처"},
    {"convention", "수칙 종류", "컨벤션"},          {"coding", "수칙 종류", "코딩 제약"},
    {"dependency", "수칙 종류", "의존성"},          {"test", "수칙 종류", "테스트"},
    {"view", "인터페이스 종류", "GUI 뷰"},           {"c_api", "인터페이스 종류", "C 함수"},
    {"module", "설계 종류", "모듈"},                 {"flow", "설계 종류", "동작 흐름"},
    {"directory", "설계 종류", "디렉토리"},          {"memory", "설계 종류", "메모리 정책"},
};

struct LegacyLink {
    std::string_view from_kind;
    std::string_view type;
    std::string_view to_kind;
    const char* relation;
};

constexpr LegacyLink LEGACY_LINKS[] = {
    {"requirement", "satisfies", "requirement", "관련 요구사항"},
    {"external_interface", "satisfies", "requirement", "관련 요구사항"},
    {"design", "satisfies", "requirement", "관련 요구사항"},
    {"design", "satisfies", "external_interface", "관련 외부 인터페이스"},
    {"design", "depends_on", "design", "의존 설계 사항"},
    {"unit_test_plan", "verifies", "design", "검증 설계 사항"},
    {"system_test_plan", "verifies", "requirement", "검증 요구사항"},
    {"e2e_test_plan", "verifies", "external_interface", "검증 외부 인터페이스"},
    {"milestone", "covers", "design", "포함 설계 사항"},
    {"milestone", "covers", "unit_test_plan", "포함 유닛 테스트"},
};

bool same(Span<const char> left, std::string_view right) { return viewOf(left) == right; }

Span<const char> keyOf(const char* key) { return boundedText(key, MAX_NAME_LENGTH); }

}

// 정의서 v0 의 단계 번호 → 설계서 v1 (11절)
int64_t mapLegacyStep(int64_t step, std::string_view purpose)
{
    if (step <= 2) { return 2; }
    if (step <= 8) { return step; }
    if (step == 9) { return 10; }
    return purpose == "release" ? 12 : 11;
}

// 없으면 빈 글
std::string_view mapLegacyKind(Span<const char> kind)
{
    for (const LegacyKind& entry : LEGACY_KINDS) {
        if (same(kind, entry.legacy)) { return entry.kind; }
    }
    return std::string_view();
}

// category → "[[tag, bit]]". 대응이 없으면 "[]"
LegacyTags legacyTags(const ModelFinder& finder, ItemKind kind, Span<const char> category)
{
    LegacyTags tags;
    tags.assign(literalText("["));
    for (const LegacyCategory& entry : LEGACY_CATEGORIES) {
        if (!same(category, entry.category)) { continue; }
        const Result<uint32_t, KiottySpecStatus> tag = finder.findTag(kind, keyOf(entry.tag));
        const Result<uint32_t, KiottySpecStatus> bit = tag ? finder.findTagBit(kind, tag.value, keyOf(entry.bit))
                                                           : Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
        if (!bit) { break; }
        tags.append(literalText("["));
        appendDecimal(tags, tag.value);
        tags.append(literalText(","));
        appendDecimal(tags, uint64_t{1} << bit.value);
        tags.append(literalText("]"));
        break;
    }
    tags.append(literalText("]"));
    return tags;
}

const char* legacyRelation(std::string_view from_kind, Span<const char> type, std::string_view to_kind)
{
    const char* relation = nullptr;
    for (const LegacyLink& link : LEGACY_LINKS) {
        if (from_kind == link.from_kind && same(type, link.type) && to_kind == link.to_kind) { relation = link.relation; }
    }
    return relation;
}

}
