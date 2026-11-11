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
Span<const char> parentOf(Span<const char> path);

KiottySpecStatus createDirectories(Span<const char> path);

int openForReading(Span<const char> path);
int openForWriting(Span<const char> path);
int readDescriptor(int descriptor, Span<char> buffer);
KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content);
void closeDescriptor(int descriptor);
bool isFile(Span<const char> path);
KiottySpecStatus removeFile(Span<const char> path);
KiottySpecStatus renameFile(Span<const char> from, Span<const char> to);

}
