
#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t READ_CHUNK_LENGTH = 65536;

KiottySpecStatus writeAll(int descriptor, Span<const char> content)
{
    size_t written = 0;
    while (written < content.length()) {
        const int count = platform::writeSome(descriptor, content.data() + written, content.length() - written);
        if (count <= 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
        written += static_cast<size_t>(count);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

size_t chunkOf(size_t room) { return room < READ_CHUNK_LENGTH ? room : READ_CHUNK_LENGTH; }

}

bool isDirectory(Span<const char> path) { return platform::isDirectory(path); }

bool isFile(Span<const char> path) { return platform::isFile(path); }

KiottySpecStatus createDirectories(Span<const char> path)
{
    const size_t root = platform::rootLength(path);
    for (size_t index = root; index < path.length(); ++index) {
        if (platform::isSeparator(path[index])) { platform::makeDirectory(Span<const char>(path.data(), index)); }
    }
    platform::makeDirectory(path);
    return platform::isDirectory(path) ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

int openForReading(Span<const char> path) { return platform::openFile(path, platform::OpenMode::READ); }

int openForWriting(Span<const char> path) { return platform::openFile(path, platform::OpenMode::WRITE); }

int readDescriptor(int descriptor, Span<char> buffer) { return platform::readSome(descriptor, buffer.data(), chunkOf(buffer.length())); }

KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content) { return writeAll(descriptor, content); }

void closeDescriptor(int descriptor) { platform::closeFile(descriptor); }

KiottySpecStatus removeFile(Span<const char> path)
{
    if (!platform::isFile(path)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return platform::removeFile(path);
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to) { return platform::renameFile(from, to); }

}
