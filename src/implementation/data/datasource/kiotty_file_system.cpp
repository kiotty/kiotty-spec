#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t READ_CHUNK_LENGTH = 65536;

size_t fitChunk(size_t room);
KiottySpecStatus writeAll(int descriptor, Span<const char> content);

}

KiottySpecStatus FileSystem::createDirectories(Span<const char> path) const
{
    const size_t root = platform::rootLength(path);
    const size_t length = path.length();
    for (size_t index = root; index < length; ++index) {
        const bool is_separator = platform::isSeparator(path[index]);
        if (!is_separator) { continue; }
        const Result<Span<const char>, KiottySpecStatus> parent = path.subspan(0, index);
        if (parent) { makeDirectory(parent.value); }
    }
    makeDirectory(path);
    const bool is_directory = isDirectory(path);
    return is_directory ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

DescriptorResult FileSystem::openForReading(Span<const char> path) const
{
    const KiottySpecStatus converted = convertSource(path);
    if (converted != KIOTTY_SPEC_STATUS_SUCCESS) { return DescriptorResult::failure(converted); }
    return platform::openFile(_area.source, platform::OpenMode::READ);
}

DescriptorResult FileSystem::openForWriting(Span<const char> path) const
{
    const KiottySpecStatus converted = convertSource(path);
    if (converted != KIOTTY_SPEC_STATUS_SUCCESS) { return DescriptorResult::failure(converted); }
    return platform::openFile(_area.source, platform::OpenMode::WRITE);
}

KiottySpecStatus FileSystem::removeFile(Span<const char> path) const
{
    const bool is_file = isFile(path);
    if (!is_file) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return platform::removeFile(_area.source);
}

KiottySpecStatus FileSystem::renameFile(Span<const char> from, Span<const char> to) const
{
    const KiottySpecStatus source = convertSource(from);
    const KiottySpecStatus target = platform::convertPath(to, _area.target.letters());
    if (source != KIOTTY_SPEC_STATUS_SUCCESS) { return source; }
    if (target != KIOTTY_SPEC_STATUS_SUCCESS) { return target; }
    return platform::renameFile(_area.source, _area.target);
}

bool FileSystem::isFile(Span<const char> path) const
{
    const KiottySpecStatus converted = convertSource(path);
    return converted == KIOTTY_SPEC_STATUS_SUCCESS && platform::isFile(_area.source);
}

bool FileSystem::isDirectory(Span<const char> path) const
{
    const KiottySpecStatus converted = convertSource(path);
    return converted == KIOTTY_SPEC_STATUS_SUCCESS && platform::isDirectory(_area.source);
}

bool FileSystem::makeDirectory(Span<const char> path) const
{
    const KiottySpecStatus converted = convertSource(path);
    return converted == KIOTTY_SPEC_STATUS_SUCCESS && platform::makeDirectory(_area.source);
}

KiottySpecStatus FileSystem::convertSource(Span<const char> path) const
{ return platform::convertPath(path, _area.source.letters()); }

int readDescriptor(int descriptor, Span<char> buffer)
{
    const Result<Span<char>, KiottySpecStatus> chunk = buffer.subspan(0, fitChunk(buffer.length()));
    return platform::readSome(descriptor, chunk.value);
}

KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content)
{ return writeAll(descriptor, content); }

void closeDescriptor(int descriptor) { platform::closeFile(descriptor); }

namespace {

size_t fitChunk(size_t room) { return room < READ_CHUNK_LENGTH ? room : READ_CHUNK_LENGTH; }

KiottySpecStatus writeAll(int descriptor, Span<const char> content)
{
    size_t written = 0;
    const size_t length = content.length();
    while (written < length) {
        const Result<Span<const char>, KiottySpecStatus> rest =
            content.subspan(written, length - written);
        if (!rest) { return rest.code; }
        const int count = platform::writeSome(descriptor, rest.value);
        if (count <= 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
        written += static_cast<size_t>(count);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

}
