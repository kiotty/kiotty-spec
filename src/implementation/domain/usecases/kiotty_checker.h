#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct UnmetCondition {
    entity::IdText id {};
    KiottySpecCondition condition {KIOTTY_SPEC_CONDITION_ENTRY_STEP};
};

static_assert(sizeof(UnmetCondition) == 20, "unexpected padding");

struct TagBit {
    static constexpr uint32_t MISSING = 0xFFFFFFFFu;

    uint64_t bit {0};
    uint32_t tag_index {MISSING};
};

static_assert(sizeof(TagBit) == 16, "unexpected padding");

// 단계별 확정 조건 (설계서 v1 5.2, 정의서 R-3 ~ R-5, R-10). 태그 · 관계 이름은 모델 표에서 찾는다.
class Checker
{
public:
    Checker(Workspace& workspace, size_t max_items) : _w(workspace), _body(entity::MAX_BODY_LENGTH + 1), _rows(max_items) {}

    Span<const UnmetCondition> collect(entity::Step step);

    // 상태 태그가 없거나 "열림" · "반영 중" 인 변경 요청
    bool isOpenChangeRequest(entity::ItemHandle handle);
    size_t openChangeRequestCount();
    // 열린 변경 요청 중 가장 상류의 진입 단계. 없으면 0
    uint32_t earliestEntryStep();
    // 본문의 "인수 조건" 제목 아래 목록 줄 수 (R-4)
    static size_t countAcceptanceCriteria(Span<const char> body);

private:
    TagBit tagBit(entity::ItemKind kind, std::string_view tag, std::string_view bit);
    uint32_t relationIndex(entity::ItemKind kind, std::string_view name);
    size_t countRelations(entity::ItemKind model, uint32_t relation, entity::ItemHandle item, bool as_target);
    void add(KiottySpecCondition condition, entity::IdText id);

    void checkEntrySteps();
    void checkAcceptanceCriteria();
    void checkMilestoneCoverage();
    void checkOneToOne(entity::ItemKind target_kind, entity::ItemKind test_kind, std::string_view relation, KiottySpecCondition pair_condition,
                       bool functional_only);
    void checkResults();

    Workspace& _w;
    std::vector<char> _body;
    std::vector<UnmetCondition> _rows;
    size_t _length {0};
};

}
