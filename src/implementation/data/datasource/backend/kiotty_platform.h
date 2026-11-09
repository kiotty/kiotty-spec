#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"

#include <cstddef>
#include <cstdint>

// 운영체제마다 다른 파일 · 경로 기본 동작 (개발수칙 6.7). 구현은 backend 의 windows · posix 파일 중 하나만 빌드된다.
namespace kiotty::spec::implementation::platform {

enum class OpenMode : uint32_t {
    READ,
    // 없으면 만들고, 있으면 비운다.
    WRITE,
};

bool isSeparator(char letter);
// 경로 비교용. Windows 는 대소문자를 가리지 않는다.
bool sameChar(char left, char right);
// "//", "C:/", "C:", "/" 같은 루트의 길이
size_t rootLength(Span<const char> path);
bool isAbsolute(Span<const char> path);
SystemPathResult absolutePath(Span<const char> path);

// 파일 기술자. 열지 못하면 음수
int openFile(Span<const char> path, OpenMode mode);
// 읽은 바이트 수. 끝이면 0, 실패면 음수
int readSome(int descriptor, char* buffer, size_t length);
int writeSome(int descriptor, const char* buffer, size_t length);
void closeFile(int descriptor);

// 이미 있어도 성공
bool makeDirectory(Span<const char> path);
bool isDirectory(Span<const char> path);
bool isFile(Span<const char> path);
KiottySpecStatus makeExecutable(Span<const char> path);
KiottySpecStatus removeFile(Span<const char> path);
KiottySpecStatus renameFile(Span<const char> from, Span<const char> to);

}
