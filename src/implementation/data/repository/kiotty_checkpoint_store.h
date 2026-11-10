#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/data/repository/kiotty_checkpoint_writer.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 체크포인트 풀 (설계서 v1 10.1). index = sequence - 1 이며, 정리로 지운 슬롯은 비어 있다.
class CheckpointStore
{
public:
    CheckpointStore(Failure& failure, size_t capacity) : _failure(failure), _records(capacity), _exists(capacity, 0) {}

    void clear();
    // 파일에서 읽은 체크포인트를 넣는다. 같은 sequence 가 두 번이면 CONFLICT
    KiottySpecStatus put(uint32_t sequence, const entity::CheckpointRecord& record);

    // 가장 최근 체크포인트의 sequence. 없으면 0
    uint32_t latest() const { return _latest; }
    Result<const entity::CheckpointRecord*, KiottySpecStatus> get(uint32_t sequence) const;
    // sequence 순서로 훑을 때 쓴다. 지운 슬롯이면 nullptr
    const entity::CheckpointRecord* at(uint32_t sequence) const;

    // 쓰기는 세부 클래스에 맡긴다 (개발수칙 4.19). 부른 자리에서 쓰고 버린다.
    CheckpointWriter writer() { return CheckpointWriter(_failure, _records, _exists, _latest); }

private:
    Failure& _failure;
    std::vector<entity::CheckpointRecord> _records;
    std::vector<uint8_t> _exists;
    uint32_t _latest {0};
};

}
