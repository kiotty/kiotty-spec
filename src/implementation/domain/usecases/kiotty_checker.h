#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 단계별 확정 조건 (설계서 v1 5.2, 정의서 R-3 ~ R-5, R-10). 태그 · 관계 이름은 모델 표에서 찾는다.
class Checker
{
public:
    explicit Checker(Workspace& workspace) : _w(workspace), _body(entity::MAX_BODY_LENGTH + 1) {}

    // 조건이 맞으면 SUCCESS, 어긋나면 PRECONDITION 과 함께 어긋난 항목 ID 를 에러 버퍼에 남긴다.
    KiottySpecStatus check(entity::Step step);

    // 상태 태그가 없거나 "열림" · "반영 중" 인 변경 요청
    bool isOpenChangeRequest(entity::ItemHandle handle);
    size_t openChangeRequestCount();
    // 열린 변경 요청 중 가장 상류의 진입 단계. 없으면 0
    uint32_t earliestEntryStep();
    // 본문의 "인수 조건" 제목 아래 목록 줄 수 (R-4)
    static size_t countAcceptanceCriteria(Span<const char> body);

private:
    class Report;

    uint64_t tagBit(entity::ItemKind kind, const char* tag, const char* bit, uint32_t& tag_index);
    uint32_t relationIndex(entity::ItemKind kind, const char* name);
    size_t countRelations(entity::ItemKind model, uint32_t relation, entity::ItemHandle item, bool as_target);

    void checkEntrySteps(Report& report);
    void checkAcceptanceCriteria(Report& report);
    void checkMilestoneCoverage(Report& report);
    void checkOneToOne(Report& report, entity::ItemKind target_kind, entity::ItemKind test_kind, const char* relation, bool functional_only);
    void checkResults(Report& report);

    Workspace& _w;
    std::vector<char> _body;
};

}
