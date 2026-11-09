// 파일 · 디렉토리 입출력. 운영체제마다 다른 부분은 backend/kiotty_platform 이 맡는다.

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

Result<size_t, KiottySpecStatus> readFile(Span<const char> path, Span<char> buffer)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const int descriptor = platform::openFile(path, platform::OpenMode::READ);
    if (descriptor < 0) { return SizeResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    size_t length = 0;
    int count = 1;
    while (count > 0 && length < buffer.length()) {
        count = platform::readSome(descriptor, buffer.data() + length, chunkOf(buffer.length() - length));
        if (count > 0) { length += static_cast<size_t>(count); }
    }
    // buffer 가 꼭 찼으면 한 바이트를 더 읽어 보아 남은 내용이 있는지 본다.
    char extra = 0;
    if (count > 0) { count = platform::readSome(descriptor, &extra, 1); }
    platform::closeFile(descriptor);
    if (count > 0) { return SizeResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    if (count < 0) { return SizeResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return SizeResult::success(length);
}

KiottySpecStatus writeWholeFile(Span<const char> path, Span<const char> content)
{
    const Span<const char> parent = parentOf(path);
    if (parent.length() > platform::rootLength(path)) { createDirectories(parent); }
    const int descriptor = platform::openFile(path, platform::OpenMode::WRITE);
    if (descriptor < 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    const KiottySpecStatus written = writeAll(descriptor, content);
    platform::closeFile(descriptor);
    return written;
}

int openForReading(Span<const char> path) { return platform::openFile(path, platform::OpenMode::READ); }

int openForWriting(Span<const char> path) { return platform::openFile(path, platform::OpenMode::WRITE); }

int readDescriptor(int descriptor, Span<char> buffer) { return platform::readSome(descriptor, buffer.data(), chunkOf(buffer.length())); }

KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content) { return writeAll(descriptor, content); }

void closeDescriptor(int descriptor) { platform::closeFile(descriptor); }

KiottySpecStatus makeExecutable(Span<const char> path) { return platform::makeExecutable(path); }

// 없는 파일이면 성공으로 본다.
KiottySpecStatus removeFile(Span<const char> path)
{
    if (!platform::isFile(path)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return platform::removeFile(path);
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to) { return platform::renameFile(from, to); }

}
