#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/data/repository/spec_file/kiotty_spec_tables.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class RuleSpecWriter
{
public:
    RuleSpecWriter(const SpecTables& tables, KispFileWriter& file, Span<char> area)
        : _tables(tables), _file(file), _area(area)
    {}

    KiottySpecStatus write(uint32_t checkpoint);

private:
    KiottySpecStatus writeMeta();
    KiottySpecStatus writeCheckpoint(uint32_t checkpoint);
    KiottySpecStatus writeItems(uint32_t checkpoint);
    KiottySpecStatus writeItemVersions(uint32_t checkpoint);
    KiottySpecStatus writeTags(uint32_t checkpoint);
    KiottySpecStatus writeTag(const entity::TagVersion& version);
    KiottySpecStatus writeBodies(uint32_t checkpoint);
    bool isRuleAt(uint32_t key, uint32_t checkpoint) const;
    bool isCurrentTag(const entity::TagVersion& version, uint32_t checkpoint) const;
    KiottySpecStatus emit(const Result<Span<const char>, KiottySpecStatus>& finished);

    SpecTables _tables;
    KispFileWriter& _file;
    Span<char> _area;
};

}
