#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
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
    CheckpointStore(Storage& storage, size_t capacity) : _storage(storage), _records(capacity), _exists(capacity, 0) {}

    KiottySpecStatus load();

    // 가장 최근 체크포인트의 sequence. 없으면 0
    uint32_t latest() const { return _latest; }
    Result<const entity::CheckpointRecord*, KiottySpecStatus> get(uint32_t sequence) const;
    // sequence 순서로 훑을 때 쓴다. 지운 슬롯이면 nullptr
    const entity::CheckpointRecord* at(uint32_t sequence) const;

    // 이 체크포인트에 묶인 버전 행 수 (항목 · 관계 · 결정 기록)
    Result<uint64_t, KiottySpecStatus> countWrites(uint32_t sequence);

    // 쓰기는 세부 클래스에 맡긴다 (개발수칙 4.19). 부른 자리에서 쓰고 버린다.
    CheckpointWriter writer() { return CheckpointWriter(_storage, _records, _exists, _latest); }

private:
    Storage& _storage;
    std::vector<entity::CheckpointRecord> _records;
    std::vector<uint8_t> _exists;
    uint32_t _latest {0};
};

}
