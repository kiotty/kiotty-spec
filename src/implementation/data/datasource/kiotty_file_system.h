#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/infra/platform/kiotty_system_path.h"

#include <cstddef>

namespace kiotty::spec::implementation {

class FileSystem
{
public:
    explicit FileSystem(PathArea& area) : _area(area) {}

    bool isFile(Span<const char> path) const;
    bool isDirectory(Span<const char> path) const;
    KiottySpecStatus createDirectories(Span<const char> path) const;
    DescriptorResult openForReading(Span<const char> path) const;
    DescriptorResult openForWriting(Span<const char> path) const;
    KiottySpecStatus removeFile(Span<const char> path) const;
    KiottySpecStatus renameFile(Span<const char> from, Span<const char> to) const;

private:
    bool makeDirectory(Span<const char> path) const;
    KiottySpecStatus convertSource(Span<const char> path) const;

    PathArea& _area;
};

Span<const char> fileNameOf(Span<const char> path);
Span<const char> parentOf(Span<const char> path);

int readDescriptor(int descriptor, Span<char> buffer);
KiottySpecStatus writeDescriptor(int descriptor, Span<const char> content);
void closeDescriptor(int descriptor);

}
