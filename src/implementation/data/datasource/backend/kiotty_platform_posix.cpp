#ifndef _WIN32

#include "implementation/data/datasource/backend/kiotty_platform.h"

#include "implementation/core/kiotty_text_view.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace kiotty::spec::implementation {

namespace platform {

namespace {

constexpr mode_t FILE_MODE = 0666;
constexpr mode_t DIRECTORY_MODE = 0777;

constexpr size_t DOUBLE_SEPARATOR_ROOT_LENGTH = 2;

using FileStatus = struct stat;
using ModeResult = Result<mode_t, KiottySpecStatus>;

ModeResult readMode(const NativePath& path);
char readLetter(Span<const char> path, size_t index);

}

}

SystemPathResult PathResolver::resolveAbsolutePath(Span<const char> path) const
{
    const size_t root = platform::rootLength(path);
    const bool is_relative = root == 0;
    Span<const char> base = path;
    Span<const char> part {};
    if (is_relative) {
        const char* const directory = ::getcwd(_area.source.text, MAX_SYSTEM_PATH_LENGTH);
        if (directory == nullptr) {
            return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT);
        }
        base = boundedText(_area.source.text, MAX_SYSTEM_PATH_LENGTH);
        part = path;
    }
    const SystemPathResult joined = joinPath(base, part);
    if (!joined) { return joined; }
    const KiottySpecStatus copied = _area.absolute.assign(_area.joined.view());
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    return SystemPathResult::success(_area.absolute);
}

namespace platform {

size_t rootLength(Span<const char> path)
{
    const char first = readLetter(path, 0);
    const char second = readLetter(path, 1);
    const bool is_double_separator = first == '/' && second == '/';
    if (is_double_separator) { return DOUBLE_SEPARATOR_ROOT_LENGTH; }
    return first == '/' ? 1 : 0;
}

bool isSeparator(char letter) { return letter == '/'; }

KiottySpecStatus convertPath(Span<const char> path, Span<NativeLetter> native)
{
    const size_t path_length = path.length();
    const bool is_too_long = path_length >= native.length();
    if (is_too_long) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    for (size_t index = 0; index < path_length; ++index) { native[index] = path[index]; }
    const Result<char&, KiottySpecStatus> terminator = native.at(path_length);
    if (!terminator) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    *terminator.value = '\0';
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

DescriptorResult openFile(const NativePath& path, OpenMode mode)
{
    const int flags = mode == OpenMode::READ ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
    const int descriptor = ::open(path.text, flags, FILE_MODE);
    if (descriptor < 0) { return DescriptorResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return DescriptorResult::success(descriptor);
}

int readSome(int descriptor, Span<char> buffer)
{ return static_cast<int>(::read(descriptor, buffer.data(), buffer.length())); }

int writeSome(int descriptor, Span<const char> content)
{ return static_cast<int>(::write(descriptor, content.data(), content.length())); }

void closeFile(int descriptor) { ::close(descriptor); }

bool makeDirectory(const NativePath& path)
{ return ::mkdir(path.text, DIRECTORY_MODE) == 0 || errno == EEXIST; }

bool isDirectory(const NativePath& path)
{
    const ModeResult mode = readMode(path);
    return mode && S_ISDIR(mode.value);
}

bool isFile(const NativePath& path)
{
    const ModeResult mode = readMode(path);
    return mode && S_ISREG(mode.value);
}

KiottySpecStatus removeFile(const NativePath& path)
{
    return ::remove(path.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                    : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus renameFile(const NativePath& from, const NativePath& to)
{
    return ::rename(from.text, to.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                             : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

namespace {

ModeResult readMode(const NativePath& path)
{
    FileStatus status {};
    const int status_code = ::stat(path.text, &status);
    if (status_code != 0) { return ModeResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return ModeResult::success(status.st_mode);
}

char readLetter(Span<const char> path, size_t index)
{
    const Result<const char&, KiottySpecStatus> letter = path.at(index);
    return letter ? *letter.value : '\0';
}

}

}

}

#endif
