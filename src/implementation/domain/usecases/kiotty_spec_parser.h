#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class SpecParser
{
public:
    explicit SpecParser(const SpecTables& tables) : _t(tables) {}

    KiottySpecStatus parse(const KispFields& fields);

private:
    KiottySpecStatus parseMeta(const KispFields& fields);
    KiottySpecStatus parseCheckpoint(const KispFields& fields);
    KiottySpecStatus parseItem(const KispFields& fields);
    KiottySpecStatus parseItemVersion(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseTag(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseRelation(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseDecision(const KispFields& fields);
    KiottySpecStatus parseBody(const KispFields& fields);
    Result<entity::ItemKind, KiottySpecStatus> kindOfKey(uint32_t key);
    Result<uint64_t, KiottySpecStatus> parseTagValue(entity::ItemKind kind, uint32_t tag, Span<const char> names) const;

    SpecTables _t;
    bool _versions_sorted {false};
};

}
