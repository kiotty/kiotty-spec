#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::platform {

enum class OpenMode : uint32_t {
    READ,
    WRITE,
};

bool isSeparator(char letter);
size_t rootLength(Span<const char> path);
SystemPathResult absolutePath(Span<const char> path);

int openFile(Span<const char> path, OpenMode mode);
int readSome(int descriptor, char* buffer, size_t length);
int writeSome(int descriptor, const char* buffer, size_t length);
void closeFile(int descriptor);

bool makeDirectory(Span<const char> path);
bool isDirectory(Span<const char> path);
bool isFile(Span<const char> path);
KiottySpecStatus removeFile(Span<const char> path);
KiottySpecStatus renameFile(Span<const char> from, Span<const char> to);

}
