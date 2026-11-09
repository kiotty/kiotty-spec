#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/core/kiotty_text_view.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <stdlib.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdio>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_PATH_PARTS = 256;
constexpr size_t READ_CHUNK_LENGTH = 65536;
constexpr char SEPARATOR[] = "/";
constexpr char CURRENT[] = ".";
constexpr char PARENT[] = "..";

bool isSeparator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

#ifdef _WIN32
bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

char lowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') { return static_cast<char>(c - 'A' + 'a'); }
    return c;
}
#endif

bool sameChar(char left, char right)
{
    if (isSeparator(left) && isSeparator(right)) { return true; }
#ifdef _WIN32
    return lowerAscii(left) == lowerAscii(right);
#else
    return left == right;
#endif
}

bool sameText(Span<const char> left, Span<const char> right)
{
    if (left.length() != right.length()) { return false; }
    for (size_t index = 0; index < left.length(); ++index) {
        if (!sameChar(left[index], right[index])) { return false; }
    }
    return true;
}

size_t rootLength(Span<const char> path)
{
    const size_t length = path.length();
    if (length >= 2 && isSeparator(path[0]) && isSeparator(path[1])) { return 2; }
#ifdef _WIN32
    const bool hasDrive = length >= 2 && isLetter(path[0]) && path[1] == ':';
    if (hasDrive && length >= 3 && isSeparator(path[2])) { return 3; }
    if (hasDrive) { return 2; }
    if (length >= 1 && isSeparator(path[0])) { return 1; }
    return 0;
#else
    if (length >= 1 && path[0] == '/') { return 1; }
    return 0;
#endif
}

bool isNamed(Span<const char> part, Span<const char> name) { return viewOf(part) == viewOf(name); }

KiottySpecStatus appendRoot(SystemPath &out, Span<const char> root)
{
    for (size_t index = 0; index < root.length(); ++index) {
        const char c = isSeparator(root[index]) ? '/' : root[index];
        const KiottySpecStatus appended = out.append(Span<const char>(&c, 1));
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

class PartStack
{
public:
    KiottySpecStatus push(Span<const char> part, bool hasRoot)
    {
        if (part.length() == 0 || isNamed(part, literalText(CURRENT))) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        if (isNamed(part, literalText(PARENT))) { return climb(part, hasRoot); }
        return keep(part);
    }

    KiottySpecStatus write(SystemPath &out) const
    {
        for (size_t index = 0; index < _count; ++index) {
            if (index > 0) { out.append(literalText(SEPARATOR)); }
            const KiottySpecStatus appended = out.append(_parts[index]);
            if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
        }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

private:
    KiottySpecStatus climb(Span<const char> part, bool hasRoot)
    {
        const bool canPop = _count > 0 && !isNamed(_parts[_count - 1], literalText(PARENT));
        if (canPop) {
            --_count;
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        if (hasRoot) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        return keep(part);
    }

    KiottySpecStatus keep(Span<const char> part)
    {
        if (_count == MAX_PATH_PARTS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        _parts[_count] = part;
        ++_count;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> _parts[MAX_PATH_PARTS];
    size_t _count {0};
};

KiottySpecStatus splitParts(Span<const char> path, size_t start, bool hasRoot, PartStack &stack)
{
    size_t begin = start;
    for (size_t index = start; index <= path.length(); ++index) {
        const bool atEnd = index == path.length();
        if (!atEnd && !isSeparator(path[index])) { continue; }
        const KiottySpecStatus pushed = stack.push(Span<const char>(path.data() + begin, index - begin), hasRoot);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
        begin = index + 1;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

size_t parentLength(Span<const char> path)
{
    size_t index = path.length();
    while (index > 0 && !isSeparator(path[index - 1])) { --index; }
    while (index > 0 && isSeparator(path[index - 1])) { --index; }
    return index;
}

#ifdef _WIN32
struct WidePath {
    wchar_t text[MAX_SYSTEM_PATH_LENGTH + 1] {};
};

using WidePathResult = Result<WidePath, KiottySpecStatus>;

WidePathResult widen(Span<const char> path)
{
    WidePath wide;
    if (path.length() == 0) { return WidePathResult::success(wide); }
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.length()), wide.text,
                                           static_cast<int>(MAX_SYSTEM_PATH_LENGTH));
    if (length <= 0) { return WidePathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    wide.text[length] = L'\0';
    return WidePathResult::success(wide);
}

SystemPathResult narrow(const wchar_t *wide)
{
    char buffer[MAX_SYSTEM_PATH_LENGTH + 1] {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, static_cast<int>(MAX_SYSTEM_PATH_LENGTH), nullptr, nullptr);
    if (length <= 0) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    SystemPath path;
    const KiottySpecStatus copied = appendRoot(path, Span<const char>(buffer, static_cast<size_t>(length - 1)));
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    return SystemPathResult::success(path);
}

int openFile(Span<const char> path, int flags)
{
    const auto wide = widen(path);
    if (!wide) { return -1; }
    int descriptor = -1;
    _wsopen_s(&descriptor, wide.value.text, flags | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return descriptor;
}

bool makeDirectory(Span<const char> path)
{
    const auto wide = widen(path);
    if (!wide) { return false; }
    return _wmkdir(wide.value.text) == 0 || errno == EEXIST;
}

int readSome(int descriptor, char *buffer, size_t length) { return _read(descriptor, buffer, static_cast<unsigned int>(length)); }
int writeSome(int descriptor, const char *buffer, size_t length) { return _write(descriptor, buffer, static_cast<unsigned int>(length)); }
void closeFile(int descriptor) { _close(descriptor); }
#else
int openFile(Span<const char> path, int flags)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return -1; }
    return ::open(terminated.c_str(), flags, 0666);
}

bool makeDirectory(Span<const char> path)
{
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return false; }
    return ::mkdir(terminated.c_str(), 0777) == 0 || errno == EEXIST;
}

int readSome(int descriptor, char *buffer, size_t length) { return static_cast<int>(::read(descriptor, buffer, length)); }
int writeSome(int descriptor, const char *buffer, size_t length) { return static_cast<int>(::write(descriptor, buffer, length)); }
void closeFile(int descriptor) { ::close(descriptor); }
#endif

#ifdef _WIN32
constexpr int READ_FLAGS = _O_RDONLY;
constexpr int WRITE_FLAGS = _O_WRONLY | _O_CREAT | _O_TRUNC;
#else
constexpr int READ_FLAGS = O_RDONLY;
constexpr int WRITE_FLAGS = O_WRONLY | O_CREAT | O_TRUNC;
#endif

KiottySpecStatus writeAll(int descriptor, Span<const char> content)
{
    size_t written = 0;
    while (written < content.length()) {
        const int count = writeSome(descriptor, content.data() + written, content.length() - written);
        if (count <= 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
        written += static_cast<size_t>(count);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

bool isAbsolutePath(Span<const char> path)
{
    const size_t root = rootLength(path);
#ifdef _WIN32
    return root >= 2 && isSeparator(path[root - 1]);
#else
    return root > 0;
#endif
}

bool isDirectory(Span<const char> path)
{
#ifdef _WIN32
    const auto wide = widen(path);
    if (!wide) { return false; }
    struct _stat64 status {};
    if (_wstat64(wide.value.text, &status) != 0) { return false; }
    return (status.st_mode & _S_IFDIR) != 0;
#else
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return false; }
    struct stat status {};
    if (::stat(terminated.c_str(), &status) != 0) { return false; }
    return S_ISDIR(status.st_mode);
#endif
}

SystemPathResult absolutePath(Span<const char> path)
{
#ifdef _WIN32
    const auto wide = widen(path);
    if (!wide) { return SystemPathResult::failure(wide.code); }
    wchar_t full[MAX_SYSTEM_PATH_LENGTH + 1] {};
    if (_wfullpath(full, wide.value.text, MAX_SYSTEM_PATH_LENGTH) == nullptr) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return narrow(full);
#else
    if (isAbsolutePath(path)) { return joinPath(path, Span<const char>()); }
    char current[MAX_SYSTEM_PATH_LENGTH + 1] {};
    if (::getcwd(current, MAX_SYSTEM_PATH_LENGTH) == nullptr) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return joinPath(boundedText(current, MAX_SYSTEM_PATH_LENGTH), path);
#endif
}

SystemPathResult joinPath(Span<const char> base, Span<const char> part)
{
    SystemPath joined;
    const KiottySpecStatus copied = appendRoot(joined, base);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    if (part.length() == 0) { return SystemPathResult::success(joined); }
    const bool needsSeparator = joined.length() > 0 && !isSeparator(base[base.length() - 1]);
    if (needsSeparator) { joined.append(literalText(SEPARATOR)); }
    const KiottySpecStatus appended = appendRoot(joined, part);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(appended); }
    return SystemPathResult::success(joined);
}

SystemPathResult normalizePath(Span<const char> path)
{
    const size_t root = rootLength(path);
    SystemPath normalized;
    const KiottySpecStatus rooted = appendRoot(normalized, Span<const char>(path.data(), root));
    if (rooted != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(rooted); }
    PartStack stack;
    const KiottySpecStatus split = splitParts(path, root, root > 0, stack);
    if (split != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(split); }
    const KiottySpecStatus written = stack.write(normalized);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(written); }
    if (normalized.empty()) { normalized.assign(literalText(CURRENT)); }
    return SystemPathResult::success(normalized);
}

SystemPathResult relativeInside(Span<const char> path, Span<const char> root)
{
    const auto normalized = normalizePath(path);
    if (!normalized) { return normalized; }
    if (!isAbsolutePath(path)) { return normalized; }
    const auto base = normalizePath(root);
    if (!base) { return SystemPathResult::success(normalized.value); }
    const Span<const char> full = normalized.value.view();
    const Span<const char> prefix = base.value.view();
    if (full.length() < prefix.length()) { return normalized; }
    if (!sameText(Span<const char>(full.data(), prefix.length()), prefix)) { return normalized; }
    if (full.length() == prefix.length()) { return normalizePath(literalText(CURRENT)); }
    const bool baseEndsWithSeparator = prefix.length() > 0 && prefix[prefix.length() - 1] == '/';
    const size_t start = baseEndsWithSeparator ? prefix.length() : prefix.length() + 1;
    const bool atBoundary = baseEndsWithSeparator || full[prefix.length()] == '/';
    if (!atBoundary) { return normalized; }
    return normalizePath(Span<const char>(full.data() + start, full.length() - start));
}

Span<const char> fileNameOf(Span<const char> path)
{
    size_t end = path.length();
    while (end > 0 && isSeparator(path[end - 1])) { --end; }
    size_t begin = end;
    while (begin > 0 && !isSeparator(path[begin - 1])) { --begin; }
    return Span<const char>(path.data() + begin, end - begin);
}

KiottySpecStatus createDirectories(Span<const char> path)
{
    const size_t root = rootLength(path);
    for (size_t index = root; index < path.length(); ++index) {
        if (!isSeparator(path[index])) { continue; }
        makeDirectory(Span<const char>(path.data(), index));
    }
    makeDirectory(path);
    if (isDirectory(path)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

std::optional<std::string> readWholeFile(Span<const char> path)
{
    const int descriptor = openFile(path, READ_FLAGS);
    if (descriptor < 0) { return std::nullopt; }
    std::string content;
    char chunk[READ_CHUNK_LENGTH];
    int count = readSome(descriptor, chunk, READ_CHUNK_LENGTH);
    while (count > 0) {
        content.append(chunk, static_cast<size_t>(count));
        count = readSome(descriptor, chunk, READ_CHUNK_LENGTH);
    }
    closeFile(descriptor);
    if (count < 0) { return std::nullopt; }
    return content;
}

Result<size_t, KiottySpecStatus> readFile(Span<const char> path, Span<char> buffer)
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const int descriptor = openFile(path, READ_FLAGS);
    if (descriptor < 0) { return SizeResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    size_t length = 0;
    int count = 0;
    do {
        const size_t room = buffer.length() - length;
        // 한 바이트를 더 읽어 보고 남으면 buffer 보다 큰 파일이다.
        char extra = 0;
        char* const target = room > 0 ? buffer.data() + length : &extra;
        const size_t asked = room > 0 ? (room < READ_CHUNK_LENGTH ? room : READ_CHUNK_LENGTH) : 1;
        count = readSome(descriptor, target, asked);
        if (count > 0 && room == 0) {
            closeFile(descriptor);
            return SizeResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
        }
        if (count > 0) { length += static_cast<size_t>(count); }
    } while (count > 0);
    closeFile(descriptor);
    if (count < 0) { return SizeResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return SizeResult::success(length);
}

KiottySpecStatus writeWholeFile(Span<const char> path, Span<const char> content)
{
    const size_t parent = parentLength(path);
    if (parent > rootLength(path)) { createDirectories(Span<const char>(path.data(), parent)); }
    const int descriptor = openFile(path, WRITE_FLAGS);
    if (descriptor < 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    const KiottySpecStatus written = writeAll(descriptor, content);
    closeFile(descriptor);
    return written;
}

KiottySpecStatus makeExecutable(Span<const char> path)
{
#ifdef _WIN32
    if (path.length() == 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
#else
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    struct stat status {};
    if (::stat(terminated.c_str(), &status) != 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    if (::chmod(terminated.c_str(), status.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) { return KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
#endif
}

bool isFile(Span<const char> path)
{
#ifdef _WIN32
    const auto wide = widen(path);
    if (!wide) { return false; }
    struct _stat64 status {};
    if (_wstat64(wide.value.text, &status) != 0) { return false; }
    return (status.st_mode & _S_IFREG) != 0;
#else
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return false; }
    struct stat status {};
    if (::stat(terminated.c_str(), &status) != 0) { return false; }
    return S_ISREG(status.st_mode);
#endif
}

KiottySpecStatus removeFile(Span<const char> path)
{
    if (!isFile(path)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
#ifdef _WIN32
    const auto wide = widen(path);
    if (!wide) { return wide.code; }
    return _wremove(wide.value.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
#else
    SystemPath terminated;
    if (terminated.assign(path) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    return ::remove(terminated.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
#endif
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to)
{
#ifdef _WIN32
    const auto source = widen(from);
    const auto target = widen(to);
    if (!source || !target) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    return _wrename(source.value.text, target.value.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
#else
    SystemPath source;
    SystemPath target;
    if (source.assign(from) != KIOTTY_SPEC_STATUS_SUCCESS || target.assign(to) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    return ::rename(source.c_str(), target.c_str()) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
#endif
}

}
