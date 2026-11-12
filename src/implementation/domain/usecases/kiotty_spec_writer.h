#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

namespace kiotty::spec::implementation {

class SpecWriter
{
public:
    SpecWriter(const SpecTables& tables, KispFileWriter& file, Span<char> area)
        : _tables(tables), _file(file), _area(area)
    {}

    KiottySpecStatus write();

private:
    KiottySpecStatus writeMeta();
    KiottySpecStatus writeCheckpoints();
    KiottySpecStatus writeItems();
    KiottySpecStatus writeItemVersions();
    KiottySpecStatus writeTags();
    KiottySpecStatus writeRelations();
    KiottySpecStatus writeDecisions();
    KiottySpecStatus writeBodies();
    KiottySpecStatus writeHeader(std::string_view columns);
    KiottySpecStatus emit(const Result<Span<const char>, KiottySpecStatus>& finished);

    SpecTables _tables;
    KispFileWriter& _file;
    Span<char> _area;
};

}
