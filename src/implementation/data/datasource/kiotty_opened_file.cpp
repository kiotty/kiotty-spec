#include "implementation/data/datasource/kiotty_opened_file.h"

namespace kiotty::spec::implementation {

namespace {

bool isOpenDescriptor(int descriptor);

}

OpenedFileResult OpenedFile::openForReading(const FileSystem& file_system, Span<const char> path)
{
    const DescriptorResult opened = file_system.openForReading(path);
    if (!opened) { return OpenedFileResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return OpenedFileResult::success(OpenedFile(opened.value));
}

OpenedFileResult OpenedFile::openForWriting(const FileSystem& file_system, Span<const char> path)
{
    const DescriptorResult opened = file_system.openForWriting(path);
    if (!opened) { return OpenedFileResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return OpenedFileResult::success(OpenedFile(opened.value));
}

OpenedFile::~OpenedFile() { close(); }

OpenedFile::OpenedFile(OpenedFile&& other) noexcept : _descriptor(other._descriptor)
{
    other._descriptor = CLOSED_DESCRIPTOR;
}

void OpenedFile::close()
{
    const bool is_open = isOpen();
    if (is_open) { closeDescriptor(_descriptor); }
    _descriptor = CLOSED_DESCRIPTOR;
}

bool OpenedFile::isOpen() const { return isOpenDescriptor(_descriptor); }

namespace {

bool isOpenDescriptor(int descriptor) { return descriptor >= 0; }

}

}
