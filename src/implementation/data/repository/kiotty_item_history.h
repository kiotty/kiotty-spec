#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 항목의 버전 표를 읽고 정리한다. ItemStore 의 세부 클래스 (개발수칙 4.19).
class ItemHistory
{
public:
    explicit ItemHistory(ItemTables& tables) : _t(tables) {}

    // 지운 항목의 ID 도 찾는다.
    Result<uint32_t, KiottySpecStatus> keyOf(Span<const char> id) const;
    Result<entity::IdText, KiottySpecStatus> idOf(uint32_t key) const;
    // 그 시점까지 항목 · 태그에 마지막으로 쓴 체크포인트. 없으면 0
    uint32_t lastWriteAt(uint32_t key, uint32_t checkpoint) const;
    // 그 시점의 태그 값. 없거나 지웠으면 0
    uint64_t tagAt(uint32_t key, uint32_t tag, uint32_t checkpoint) const;
    // 두 시점 사이에 태그 값이 하나라도 다른지
    bool tagsDiffer(uint32_t key, uint32_t from, uint32_t to) const;
    // "<prefix>-<번호>" 의 다음 번호 (세 자리 이상). 지운 항목의 ID 도 센다.
    Result<entity::IdText, KiottySpecStatus> nextId(Span<const char> prefix) const;
    // 버전이 있는 모든 key 를 (Kind, ID) 순으로. 정렬 배열을 빌려 쓰므로 다음 호출 전까지만 유효하다.
    Span<const uint32_t> keysByKindAndId();
    // 정리 (설계서 v1 5.3). 버전이 없어진 key 의 ID 줄과 본문도 지운다.
    void prune(uint32_t base);

private:
    Span<const entity::TagVersion> tagsOf(uint32_t key) const;

    ItemTables& _t;
};

}
