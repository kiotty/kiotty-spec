#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/data/datasource/kiotty_opened_file.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace kiotty::spec::implementation {

constexpr uint64_t EMPTY_KISP_HASH = 0xcbf29ce484222325ull;
constexpr char KISP_TEMPORARY_SUFFIX[] = ".tmp";

class KispFileReader
{
public:
    KispFileReader(OpenedFile file, Span<char> buffer, Span<char> decode_area)
        : _file(std::move(file), buffer), _decoder(decode_area)
    {}

    Result<bool, KiottySpecStatus> next();
    Result<KispFields, KiottySpecStatus> fields() const { return _decoder.decode(_file.line()); }
    Span<const char> line() const { return _file.line(); }
    size_t lineNumber() const { return _line_number; }
    uint64_t hash() const { return _hash; }

private:
    FileReader _file;
    KispLineDecoder _decoder;
    size_t _line_number {0};
    uint64_t _hash {EMPTY_KISP_HASH};
};

class KispFileWriter
{
public:
    KispFileWriter(const FileSystem& file_system, OpenedFile temporary_file,
                   Span<const char> path, Span<const char> temporary_path)
        : _file_system(file_system), _file(std::move(temporary_file)), _path(path),
          _temporary_path(temporary_path)
    {}
    ~KispFileWriter();

    KispFileWriter(const KispFileWriter&) = delete;
    KispFileWriter& operator=(const KispFileWriter&) = delete;

    KiottySpecStatus write(Span<const char> line);
    KiottySpecStatus commit();
    uint64_t hash() const { return _hash; }

private:
    const FileSystem& _file_system;
    FileWriter _file;
    Span<const char> _path;
    Span<const char> _temporary_path;
    uint64_t _hash {EMPTY_KISP_HASH};
};

OpenedFileResult openKispFile(const FileSystem& file_system, Span<const char> path);

Result<uint64_t, KiottySpecStatus> hashKispFile(const FileSystem& file_system,
                                                Span<const char> path, Span<char> buffer);

}
