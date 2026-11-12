#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct WorkAreas {
    Span<char> buffer {};
    Span<char> line {};
};

static_assert(sizeof(WorkAreas) == 32, "unexpected padding");

class SpecFile
{
public:
    SpecFile(const SpecTables& tables, TextBuffer& work) : _tables(tables), _work(work) {}

    KiottySpecStatus open(Span<const char> database);
    KiottySpecStatus load();
    KiottySpecStatus save();
    KiottySpecStatus checkUnchanged();

private:
    void clear();
    Result<uint64_t, KiottySpecStatus> readLines();
    KiottySpecStatus readLine(const KispFileReader& reader);
    KiottySpecStatus finishLoad();
    KiottySpecStatus lineFailure(size_t line_number, KiottySpecStatus status);
    WorkAreas splitWork();

    SpecTables _tables;
    TextBuffer& _work;
    SystemPath _database {};
    uint64_t _hash {EMPTY_KISP_HASH};
};

}
