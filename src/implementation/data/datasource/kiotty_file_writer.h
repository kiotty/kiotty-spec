#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

class FileWriter
{
public:
    FileWriter() = default;
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    KiottySpecStatus open(Span<const char> path);
    KiottySpecStatus write(Span<const char> content);
    void close();

private:
    int _descriptor {CLOSED_DESCRIPTOR};
};

}
