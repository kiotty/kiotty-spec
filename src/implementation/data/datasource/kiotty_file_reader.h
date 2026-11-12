#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_opened_file.h"

#include <cstddef>
#include <utility>

namespace kiotty::spec::implementation {

class FileReader
{
public:
    FileReader(OpenedFile file, Span<char> buffer) : _buffer(buffer), _file(std::move(file)) {}

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    Result<bool, KiottySpecStatus> next();
    Span<const char> line() const { return _line; }

private:
    Span<char> _buffer;
    Span<const char> _line {};
    size_t _start {0};
    size_t _end {0};
    OpenedFile _file;
    bool _is_finished {false};
};

}
