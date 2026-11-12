#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr uint64_t EMPTY_KISP_HASH = 0xcbf29ce484222325ull;

class KispFileReader
{
public:
    KispFileReader(const FileSystem& file_system, Span<char> buffer, Span<char> decode_area)
        : _file_system(file_system), _file(file_system, buffer), _decoder(decode_area)
    {}

    KiottySpecStatus open(Span<const char> path);
    Result<bool, KiottySpecStatus> next();
    Result<KispFields, KiottySpecStatus> fields() const { return _decoder.decode(_file.line()); }
    Span<const char> line() const { return _file.line(); }
    size_t lineNumber() const { return _line_number; }
    uint64_t hash() const { return _hash; }

private:
    const FileSystem& _file_system;
    FileReader _file;
    KispLineDecoder _decoder;
    size_t _line_number {0};
    uint64_t _hash {EMPTY_KISP_HASH};
    bool _is_present {false};
};

class KispFileWriter
{
public:
    explicit KispFileWriter(const FileSystem& file_system)
        : _file_system(file_system), _file(file_system)
    {}
    ~KispFileWriter();

    KispFileWriter(const KispFileWriter&) = delete;
    KispFileWriter& operator=(const KispFileWriter&) = delete;

    KiottySpecStatus open(Span<const char> path);
    KiottySpecStatus write(Span<const char> line);
    KiottySpecStatus commit();
    uint64_t hash() const { return _hash; }

private:
    const FileSystem& _file_system;
    FileWriter _file;
    SystemPath _path {};
    SystemPath _temporary {};
    uint64_t _hash {EMPTY_KISP_HASH};
    bool _is_open {false};
};

Result<uint64_t, KiottySpecStatus> hashKispFile(const FileSystem& file_system,
                                                Span<const char> path, Span<char> buffer);

}
