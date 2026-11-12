#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_opened_file.h"

#include <utility>

namespace kiotty::spec::implementation {

class FileWriter
{
public:
    explicit FileWriter(OpenedFile file) : _file(std::move(file)) {}

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    KiottySpecStatus write(Span<const char> content);
    bool isOpen() const { return _file.isOpen(); }
    void close() { _file.close(); }

private:
    OpenedFile _file;
};

}
