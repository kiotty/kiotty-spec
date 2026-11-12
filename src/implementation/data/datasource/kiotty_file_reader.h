#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"

#include <cstddef>

namespace kiotty::spec::implementation {

class FileReader
{
public:
    explicit FileReader(Span<char> buffer) : _buffer(buffer) {}
    ~FileReader();

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    KiottySpecStatus open(Span<const char> path);
    Result<bool, KiottySpecStatus> next();
    Span<const char> line() const { return _line; }

private:
    Span<char> _buffer;
    Span<const char> _line {};
    int _descriptor {CLOSED_DESCRIPTOR};
    size_t _start {0};
    size_t _end {0};
    bool _is_finished {false};
};

}
