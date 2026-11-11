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

bool readStatus(Span<const char> path, struct stat& status);

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
    const size_t length = path.length();
    if (length >= DOUBLE_SEPARATOR_ROOT_LENGTH && path[0] == '/' && path[1] == '/') {
        return DOUBLE_SEPARATOR_ROOT_LENGTH;
    }
    return length >= 1 && path[0] == '/' ? 1 : 0;
}

bool isSeparator(char letter) { return letter == '/'; }

int openFile(Span<const char> path, OpenMode mode)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return -1; }
    const int flags = mode == OpenMode::READ ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
    return ::open(terminated.c_str(), flags, FILE_MODE);
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
    return ::mkdir(terminated.c_str(), DIRECTORY_MODE) == 0 || errno == EEXIST;
}

bool isDirectory(Span<const char> path)
{
    struct stat status {};
    return readStatus(path, status) && S_ISDIR(status.st_mode);
}

bool isFile(Span<const char> path)
{
    struct stat status {};
    return readStatus(path, status) && S_ISREG(status.st_mode);
}

KiottySpecStatus removeFile(Span<const char> path)
{
    SystemPath terminated {};
    const KiottySpecStatus assigned = terminated.assign(path);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) {
        return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    }
    return ::remove(terminated.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
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
    return ::rename(source.c_str(), target.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                                         : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

namespace {

bool readStatus(Span<const char> path, struct stat& status)
{
    SystemPath terminated {};
    return terminated.assign(path) == KIOTTY_SPEC_STATUS_SUCCESS &&
           ::stat(terminated.c_str(), &status) == 0;
}

}

}

#endif
