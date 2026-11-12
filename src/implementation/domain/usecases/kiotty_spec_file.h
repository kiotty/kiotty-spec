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
    SpecFile(const SpecTables& tables, TextBuffer& work, const FileSystem& file_system,
             const SystemPath& database)
        : _tables(tables), _work(work), _file_system(file_system), _database(database)
    {}

    KiottySpecStatus load();
    KiottySpecStatus save();
    KiottySpecStatus checkUnchanged();

private:
    void clear();
    Result<uint64_t, KiottySpecStatus> readLines();
    Result<uint64_t, KiottySpecStatus> readOpenedLines(OpenedFile file);
    OpenedFileResult openTemporaryFile();
    KiottySpecStatus writeLines(OpenedFile temporary_file);
    KiottySpecStatus readLine(const KispFileReader& reader);
    KiottySpecStatus finishLoad();
    KiottySpecStatus lineFailure(size_t line_number, KiottySpecStatus status);
    WorkAreas splitWork();

    SpecTables _tables;
    TextBuffer& _work;
    const FileSystem& _file_system;
    const SystemPath& _database;
    SystemPath _temporary_path {};
    uint64_t _hash {EMPTY_KISP_HASH};
};

}
