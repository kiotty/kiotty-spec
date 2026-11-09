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

bool statOf(Span<const char> path, struct stat& status)
{
    SystemPath terminated;
    return terminated.assign(path) == KIOTTY_SPEC_STATUS_SUCCESS && ::stat(terminated.c_str(), &status) == 0;
}

}

bool isSeparator(char letter) { return letter == '/'; }

bool sameChar(char left, char right) { return left == right; }

size_t rootLength(Span<const char> path)
{
    const size_t length = path.length();
    if (length >= 2 && path[0] == '/' && path[1] == '/') { return 2; }
    return length >= 1 && path[0] == '/' ? 1 : 0;
}

bool isAbsolute(Span<const char> path) { return rootLength(path) > 0; }

SystemPathResult absolutePath(Span<const char> path)
{
    if (isAbsolute(path)) { return joinPath(path, Span<const char>()); }
    char current[MAX_SYSTEM_PATH_LENGTH + 1] {};
    if (::getcwd(current, MAX_SYSTEM_PATH_LENGTH) == nullptr) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return joinPath(boundedText(current, MAX_SYSTEM_PATH_LENGTH), path);
}

int openFile(Span<const char> path, OpenMode mode)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return -1; }
    const int flags = mode == OpenMode::READ ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
    return ::open(terminated.c_str(), flags, FILE_MODE);
}

int readSome(int descriptor, char* buffer, size_t length) { return static_cast<int>(::read(descriptor, buffer, length)); }

int writeSome(int descriptor, const char* buffer, size_t length) { return static_cast<int>(::write(descriptor, buffer, length)); }

void closeFile(int descriptor) { ::close(descriptor); }

bool makeDirectory(Span<const char> path)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return false; }
    return ::mkdir(terminated.c_str(), DIRECTORY_MODE) == 0 || errno == EEXIST;
}

bool isDirectory(Span<const char> path)
{
    struct stat status {};
    return statOf(path, status) && S_ISDIR(status.st_mode);
}

bool isFile(Span<const char> path)
{
    struct stat status {};
    return statOf(path, status) && S_ISREG(status.st_mode);
}

KiottySpecStatus makeExecutable(Span<const char> path)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    struct stat status {};
    if (::stat(terminated.c_str(), &status) != 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    if (::chmod(terminated.c_str(), status.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus removeFile(Span<const char> path)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    return ::remove(terminated.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to)
{
    SystemPath source;
    SystemPath target;
    if (source.assign(from) != KIOTTY_SPEC_STATUS_SUCCESS || target.assign(to) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    return ::rename(source.c_str(), target.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

}

#endif
