#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <optional>
#include <string>

namespace kiotty::spec::implementation {

constexpr size_t MAX_SYSTEM_PATH_LENGTH = 1024;

using SystemPath = FixedString<MAX_SYSTEM_PATH_LENGTH>;
using SystemPathResult = Result<SystemPath, KiottySpecStatus>;

bool isAbsolutePath(Span<const char> path);
bool isDirectory(Span<const char> path);
SystemPathResult absolutePath(Span<const char> path);
SystemPathResult joinPath(Span<const char> base, Span<const char> part);
SystemPathResult normalizePath(Span<const char> path);
SystemPathResult relativeInside(Span<const char> path, Span<const char> root);
Span<const char> fileNameOf(Span<const char> path);

KiottySpecStatus createDirectories(Span<const char> path);
std::optional<std::string> readWholeFile(Span<const char> path);
KiottySpecStatus writeWholeFile(Span<const char> path, Span<const char> content);
KiottySpecStatus makeExecutable(Span<const char> path);
bool isFile(Span<const char> path);
// 없는 파일이면 성공으로 본다.
KiottySpecStatus removeFile(Span<const char> path);
KiottySpecStatus renameFile(Span<const char> from, Span<const char> to);

}
