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

// CheckpointStore 의 쓰는 쪽 (개발수칙 4.19). 멤버는 CheckpointStore 가 소유하고 여기서는 reference 만 든다.
// DB 에 먼저 쓰고, 성공하면 풀을 고친다.
class CheckpointWriter
{
public:
    CheckpointWriter(Storage& storage, std::vector<entity::CheckpointRecord>& records, std::vector<uint8_t>& exists, uint32_t& latest)
        : _storage(storage), _records(records), _exists(exists), _latest(latest)
    {
    }

    Result<uint32_t, KiottySpecStatus> create(entity::Step step, Span<const char> description, int64_t created_at);
    KiottySpecStatus setState(uint32_t sequence, entity::CheckpointState state);
    // base 이전의 체크포인트를 지우고 base 의 previous 를 비운다. DB 의 버전 정리는 VersionHistory 가 한다.
    Result<size_t, KiottySpecStatus> dropBefore(uint32_t base);

private:
    Storage& _storage;
    std::vector<entity::CheckpointRecord>& _records;
    std::vector<uint8_t>& _exists;
    uint32_t& _latest;
};

}
