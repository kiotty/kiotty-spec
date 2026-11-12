#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

class OpenedFile;

using OpenedFileResult = Result<OpenedFile, KiottySpecStatus>;

class OpenedFile
{
public:
    ~OpenedFile();
    OpenedFile(OpenedFile&& other) noexcept;

    OpenedFile(const OpenedFile&) = delete;
    OpenedFile& operator=(const OpenedFile&) = delete;
    OpenedFile& operator=(OpenedFile&&) = delete;

    static OpenedFileResult openForReading(const FileSystem& file_system, Span<const char> path);
    static OpenedFileResult openForWriting(const FileSystem& file_system, Span<const char> path);

    int descriptor() const { return _descriptor; }
    bool isOpen() const;
    void close();

private:
    friend struct Result<OpenedFile, KiottySpecStatus>;

    OpenedFile() = default;
    explicit OpenedFile(int descriptor) : _descriptor(descriptor) {}

    int _descriptor {CLOSED_DESCRIPTOR};
};

}
