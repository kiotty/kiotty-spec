
#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t READ_CHUNK_LENGTH = 65536;

size_t fitChunk(size_t room);
KiottySpecStatus writeAll(int descriptor, Span<const char> content);

}

KiottySpecStatus createDirectories(Span<const char> path)
{
    const size_t root = platform::rootLength(path);
    const size_t length = path.length();
    for (size_t index = root; index < length; ++index) {
        const bool is_separator = platform::isSeparator(path[index]);
        if (is_separator) {
            platform::makeDirectory(Span<const char>(path.data(), index));
        }
    }
    platform::makeDirectory(path);
    return platform::isDirectory(path) ? KIOTTY_SPEC_STATUS_SUCCESS
                                       : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

bool isDirectory(Span<const char> path) { return platform::isDirectory(path); }

int openForReading(Span<const char> path)
{ return platform::openFile(path, platform::OpenMode::READ); }

int openForWriting(Span<const char> path)
{ return platform::openFile(path, platform::OpenMode::WRITE); }

int readDescriptor(int descriptor, Span<char> buffer)
{ return platform::readSome(descriptor, buffer.data(), fitChunk(buffer.length())); }

KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content)
{ return writeAll(descriptor, content); }

void closeDescriptor(int descriptor) { platform::closeFile(descriptor); }

KiottySpecStatus removeFile(Span<const char> path)
{
    const bool is_file = platform::isFile(path);
    if (!is_file) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return platform::removeFile(path);
}

bool isFile(Span<const char> path) { return platform::isFile(path); }

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to)
{ return platform::renameFile(from, to); }

namespace {

size_t fitChunk(size_t room) { return room < READ_CHUNK_LENGTH ? room : READ_CHUNK_LENGTH; }

KiottySpecStatus writeAll(int descriptor, Span<const char> content)
{
    size_t written = 0;
    const size_t length = content.length();
    while (written < length) {
        const int count =
            platform::writeSome(descriptor, content.data() + written, content.length() - written);
        if (count <= 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
        written += static_cast<size_t>(count);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

}
