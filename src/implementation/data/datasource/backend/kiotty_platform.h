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
KiottySpecStatus convertPath(Span<const char> path, Span<NativeLetter> native);

DescriptorResult openFile(const NativePath& path, OpenMode mode);
int readSome(int descriptor, char* buffer, size_t length);
int writeSome(int descriptor, const char* buffer, size_t length);
void closeFile(int descriptor);

bool makeDirectory(const NativePath& path);
bool isDirectory(const NativePath& path);
bool isFile(const NativePath& path);
KiottySpecStatus removeFile(const NativePath& path);
KiottySpecStatus renameFile(const NativePath& from, const NativePath& to);

}
