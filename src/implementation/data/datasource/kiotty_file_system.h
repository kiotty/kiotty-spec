#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t MAX_SYSTEM_PATH_LENGTH = 1024;

using SystemPath = FixedString<MAX_SYSTEM_PATH_LENGTH>;
using SystemPathResult = Result<SystemPath, KiottySpecStatus>;

bool isDirectory(Span<const char> path);
SystemPathResult absolutePath(Span<const char> path);
SystemPathResult joinPath(Span<const char> base, Span<const char> part);
Span<const char> fileNameOf(Span<const char> path);
// 마지막 구분자 앞까지. 구분자가 없으면 빈 글
Span<const char> parentOf(Span<const char> path);

KiottySpecStatus createDirectories(Span<const char> path);

// FileReader · FileWriter 가 쓰는 파일 기술자. 열지 못하면 음수
int openForReading(Span<const char> path);
int openForWriting(Span<const char> path);
// 읽은 바이트 수. 끝이면 0, 실패면 음수
int readDescriptor(int descriptor, Span<char> buffer);
KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content);
void closeDescriptor(int descriptor);
bool isFile(Span<const char> path);
// 없는 파일이면 성공으로 본다.
KiottySpecStatus removeFile(Span<const char> path);
// to 가 있으면 바꾼다. 같은 디렉토리 안에서는 원자적이다.
KiottySpecStatus renameFile(Span<const char> from, Span<const char> to);

}
