#ifdef _WIN32

#include "implementation/data/datasource/backend/kiotty_platform.h"

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

#include <cerrno>
#include <cstdio>

namespace kiotty::spec::implementation::platform {

namespace {

struct WidePath {
    wchar_t text[MAX_SYSTEM_PATH_LENGTH + 1] {};
};

using WidePathResult = Result<WidePath, KiottySpecStatus>;

bool isLetter(char letter) { return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'); }

char lowerAscii(char letter)
{
    if (letter >= 'A' && letter <= 'Z') { return static_cast<char>(letter - 'A' + 'a'); }
    return letter;
}

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

// UTF-8 로 바꾸고 구분자를 '/' 로 맞춘다.
SystemPathResult narrow(const wchar_t* wide)
{
    char buffer[MAX_SYSTEM_PATH_LENGTH + 1] {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, static_cast<int>(MAX_SYSTEM_PATH_LENGTH), nullptr, nullptr);
    if (length <= 0) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    for (int index = 0; index < length - 1; ++index) { buffer[index] = isSeparator(buffer[index]) ? '/' : buffer[index]; }
    SystemPath path;
    const KiottySpecStatus copied = path.assign(Span<const char>(buffer, static_cast<size_t>(length - 1)));
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    return SystemPathResult::success(path);
}

bool statOf(Span<const char> path, struct _stat64& status)
{
    const WidePathResult wide = widen(path);
    return wide && _wstat64(wide.value.text, &status) == 0;
}

}

bool isSeparator(char letter) { return letter == '/' || letter == '\\'; }

bool sameChar(char left, char right)
{
    if (isSeparator(left) && isSeparator(right)) { return true; }
    return lowerAscii(left) == lowerAscii(right);
}

size_t rootLength(Span<const char> path)
{
    const size_t length = path.length();
    if (length >= 2 && isSeparator(path[0]) && isSeparator(path[1])) { return 2; }
    const bool has_drive = length >= 2 && isLetter(path[0]) && path[1] == ':';
    if (has_drive && length >= 3 && isSeparator(path[2])) { return 3; }
    if (has_drive) { return 2; }
    return length >= 1 && isSeparator(path[0]) ? 1 : 0;
}

bool isAbsolute(Span<const char> path)
{
    const size_t root = rootLength(path);
    return root >= 2 && isSeparator(path[root - 1]);
}

SystemPathResult absolutePath(Span<const char> path)
{
    const WidePathResult wide = widen(path);
    if (!wide) { return SystemPathResult::failure(wide.code); }
    wchar_t full[MAX_SYSTEM_PATH_LENGTH + 1] {};
    if (_wfullpath(full, wide.value.text, MAX_SYSTEM_PATH_LENGTH) == nullptr) { return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return narrow(full);
}

int openFile(Span<const char> path, OpenMode mode)
{
    const WidePathResult wide = widen(path);
    if (!wide) { return -1; }
    const int flags = mode == OpenMode::READ ? _O_RDONLY : (_O_WRONLY | _O_CREAT | _O_TRUNC);
    int descriptor = -1;
    _wsopen_s(&descriptor, wide.value.text, flags | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return descriptor;
}

int readSome(int descriptor, char* buffer, size_t length) { return _read(descriptor, buffer, static_cast<unsigned int>(length)); }

int writeSome(int descriptor, const char* buffer, size_t length) { return _write(descriptor, buffer, static_cast<unsigned int>(length)); }

void closeFile(int descriptor) { _close(descriptor); }

bool makeDirectory(Span<const char> path)
{
    const WidePathResult wide = widen(path);
    if (!wide) { return false; }
    return _wmkdir(wide.value.text) == 0 || errno == EEXIST;
}

bool isDirectory(Span<const char> path)
{
    struct _stat64 status {};
    return statOf(path, status) && (status.st_mode & _S_IFDIR) != 0;
}

bool isFile(Span<const char> path)
{
    struct _stat64 status {};
    return statOf(path, status) && (status.st_mode & _S_IFREG) != 0;
}

// Windows 에는 실행 비트가 없다.
KiottySpecStatus makeExecutable(Span<const char> path)
{
    return path.length() == 0 ? KIOTTY_SPEC_STATUS_INVALID_ARGUMENT : KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus removeFile(Span<const char> path)
{
    const WidePathResult wide = widen(path);
    if (!wide) { return wide.code; }
    return _wremove(wide.value.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus renameFile(Span<const char> from, Span<const char> to)
{
    const WidePathResult source = widen(from);
    const WidePathResult target = widen(to);
    if (!source || !target) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const BOOL moved = MoveFileExW(source.value.text, target.value.text, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return moved != 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

}

#endif
