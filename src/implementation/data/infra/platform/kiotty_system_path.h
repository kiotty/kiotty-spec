#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t MAX_SYSTEM_PATH_LENGTH = 1024;
constexpr int CLOSED_DESCRIPTOR = -1;

#ifdef _WIN32
using NativeLetter = wchar_t;
#else
using NativeLetter = char;
#endif

struct NativePath {
    NativeLetter text[MAX_SYSTEM_PATH_LENGTH + 1] {};

    Span<NativeLetter> letters() { return Span<NativeLetter>(text, MAX_SYSTEM_PATH_LENGTH + 1); }
};

using SystemPath = FixedString<MAX_SYSTEM_PATH_LENGTH>;
using SystemPathResult = Result<const SystemPath&, KiottySpecStatus>;
using DescriptorResult = Result<int, KiottySpecStatus>;

struct PathArea {
    NativePath source {};
    NativePath target {};
    SystemPath absolute {};
    SystemPath joined {};
};

#ifdef _WIN32
static_assert(sizeof(NativePath) == 2050, "unexpected padding");
static_assert(sizeof(PathArea) == 6150, "unexpected padding");
#else
static_assert(sizeof(NativePath) == 1025, "unexpected padding");
static_assert(sizeof(PathArea) == 4100, "unexpected padding");
#endif

class PathResolver
{
public:
    explicit PathResolver(PathArea& area) : _area(area) {}

    SystemPathResult resolveAbsolutePath(Span<const char> path) const;
    SystemPathResult joinPath(Span<const char> base, Span<const char> part) const;

private:
    PathArea& _area;
};

}
