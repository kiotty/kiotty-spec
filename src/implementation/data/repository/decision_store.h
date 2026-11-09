#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/domain/entity/kind.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DECISION_BYTES = 2048;

// 결정 기록 (설계서 v1 6). 종류당 하나이고 로그가 아니다. 풀에 두지 않고 DB 에서 호출자 버퍼로 읽는다.
class DecisionStore
{
public:
    explicit DecisionStore(Storage& storage) : _storage(storage) {}

    KiottySpecStatus write(entity::ItemKind kind, Span<const char> text, uint32_t sequence);
    // 성공하면 쓴 바이트 수. 버퍼가 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수(null 포함)
    Result<size_t, KiottySpecStatus> read(entity::ItemKind kind, char* buffer, size_t capacity);

private:
    Storage& _storage;
};

// 정리 (설계서 v1 5.3). base 이전의 버전을 base 로 올리거나 지운다.
KiottySpecStatus pruneVersions(Storage& storage, uint32_t base);

}
