#ifndef _WIN32

#include "implementation/data/datasource/backend/kiotty_platform.h"

#include "implementation/core/kiotty_text_view.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace kiotty::spec::implementation::platform {

namespace {

constexpr mode_t FILE_MODE = 0666;
constexpr mode_t DIRECTORY_MODE = 0777;

constexpr size_t DOUBLE_SEPARATOR_ROOT_LENGTH = 2;

using FileStatus = struct stat;
using ModeResult = Result<mode_t, KiottySpecStatus>;

ModeResult readMode(Span<const char> path);
char readLetter(Span<const char> path, size_t index);

}

SystemPathResult absolutePath(Span<const char> path)
{
    const size_t root = rootLength(path);
    if (root > 0) { return joinPath(path, Span<const char>()); }
    char current[MAX_SYSTEM_PATH_LENGTH + 1] {};
    const char* const directory = ::getcwd(current, MAX_SYSTEM_PATH_LENGTH);
    if (directory == nullptr) {
        return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT);
    }
    return joinPath(boundedText(current, MAX_SYSTEM_PATH_LENGTH), path);
}

size_t rootLength(Span<const char> path)
{
    const char first = readLetter(path, 0);
    const char second = readLetter(path, 1);
    const bool is_double_separator = first == '/' && second == '/';
    if (is_double_separator) { return DOUBLE_SEPARATOR_ROOT_LENGTH; }
    return first == '/' ? 1 : 0;
}

bool isSeparator(char letter) { return letter == '/'; }

DescriptorResult openFile(Span<const char> path, OpenMode mode)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return DescriptorResult::failure(assigned); }
    const int flags = mode == OpenMode::READ ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
    const int descriptor = ::open(terminated.terminated(), flags, FILE_MODE);
    if (descriptor < 0) { return DescriptorResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return DescriptorResult::success(descriptor);
}

int readSome(int descriptor, char* buffer, size_t length)
{ return static_cast<int>(::read(descriptor, buffer, length)); }

int writeSome(int descriptor, const char* buffer, size_t length)
{ return static_cast<int>(::write(descriptor, buffer, length)); }

void closeFile(int descriptor) { ::close(descriptor); }

bool makeDirectory(Span<const char> path)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return false; }
    return ::mkdir(terminated.terminated(), DIRECTORY_MODE) == 0 || errno == EEXIST;
}

bool isDirectory(Span<const char> path)
{
    const ModeResult mode = readMode(path);
    return mode && S_ISDIR(mode.value);
}

bool isFile(Span<const char> path)
{
    const ModeResult mode = readMode(path);
    return mode && S_ISREG(mode.value);
}

KiottySpecStatus removeFile(Span<const char> path)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    return ::remove(terminated.terminated()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                             : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to)
{
    SystemPath source {};
    SystemPath target {};
    const KiottySpecStatus source_assigned = source.assign(from);
    if (source_assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    const KiottySpecStatus target_assigned = target.assign(to);
    if (target_assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    return ::rename(source.terminated(), target.terminated()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                                         : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

namespace {

ModeResult readMode(Span<const char> path)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return ModeResult::failure(assigned); }
    FileStatus status {};
    const int status_code = ::stat(terminated.terminated(), &status);
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

#endif
