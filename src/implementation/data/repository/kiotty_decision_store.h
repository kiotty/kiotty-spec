#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DECISION_BYTES = entity::MAX_DECISION_VERSION_LENGTH;

// 결정 기록 (설계서 v1 6). 종류당 하나이고 로그가 아니다. 체크포인트마다 버전을 둔다.
class DecisionStore
{
public:
    DecisionStore(Failure& failure, size_t versions) : _failure(failure), _versions(versions) {}

    VersionTable<entity::DecisionVersion>& versions() { return _versions; }
    KiottySpecStatus write(entity::ItemKind kind, Span<const char> text, uint32_t sequence);
    // 그 시점의 결정 기록. 성공하면 쓴 바이트 수, 버퍼가 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수(null 포함)
    Result<size_t, KiottySpecStatus> read(entity::ItemKind kind, uint32_t checkpoint, char* buffer, size_t capacity) const;

private:
    Failure& _failure;
    VersionTable<entity::DecisionVersion> _versions;
};

}
