#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DECISION_BYTES = entity::MAX_DECISION_VERSION_LENGTH;

class DecisionStore
{
public:
    DecisionStore(entity::Failure& failure, size_t versions)
        : _failure(failure), _versions(versions)
    {}

    VersionTable<entity::DecisionVersion>& versions() { return _versions; }
    KiottySpecStatus write(entity::ItemKind kind, Span<const char> text, uint32_t sequence);
    Result<size_t, KiottySpecStatus> read(entity::ItemKind kind, uint32_t checkpoint,
                                          Span<char> buffer) const;

private:
    entity::Failure& _failure;
    VersionTable<entity::DecisionVersion> _versions;
};

}
