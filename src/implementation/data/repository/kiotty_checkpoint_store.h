#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
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

    Result<uint32_t, KiottySpecStatus> create(entity::Step step, Span<const char> description, int64_t created_at);
    KiottySpecStatus setState(uint32_t sequence, entity::CheckpointState state);
    // base 이전의 체크포인트를 지우고 base 의 previous 를 비운다. DB 의 버전 정리는 VersionHistory 가 한다.
    Result<size_t, KiottySpecStatus> dropBefore(uint32_t base);
    // 이 체크포인트에 묶인 버전 행 수 (항목 · 관계 · 결정 기록)
    Result<uint64_t, KiottySpecStatus> countWrites(uint32_t sequence);

private:
    Storage& _storage;
    std::vector<entity::CheckpointRecord> _records;
    std::vector<uint8_t> _exists;
    uint32_t _latest {0};
};

}
