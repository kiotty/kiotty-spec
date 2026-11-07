#include "implementation/data/datasource/project_files.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char MISSING_HASH[] = "missing";

}

PathResult ProjectFiles::normalize(Span<const char> path) const
{
    const std::string_view trimmed = trim(viewOf(path));
    if (trimmed.empty()) { return PathResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("경로가 비어 있음")})); }
    const auto inside = relativeInside(spanOf(trimmed), _root.view());
    if (!inside) { return PathResult::failure(_failure.raise(inside.code, {literalText("경로가 너무 김: "), spanOf(trimmed)})); }
    PathText normalized;
    const KiottySpecStatus copied = normalized.assign(inside.value.view());
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return PathResult::failure(_failure.raise(copied, {literalText("경로가 너무 김: "), spanOf(trimmed)})); }
    return PathResult::success(normalized);
}

HashText ProjectFiles::hashOf(Span<const char> normalizedPath) const
{
    HashText hash;
    const auto content = read(normalizedPath);
    if (!content) {
        hash.assign(literalText(MISSING_HASH));
        return hash;
    }
    hash.assign(spanOf(fnv1a64Hex(*content)));
    return hash;
}

std::optional<std::string> ProjectFiles::read(Span<const char> normalizedPath) const
{
    const auto located = locate(normalizedPath);
    if (!located) { return std::nullopt; }
    return readWholeFile(located.value.view());
}

SystemPathResult ProjectFiles::locate(Span<const char> normalizedPath) const
{
    if (isAbsolutePath(normalizedPath)) { return joinPath(normalizedPath, Span<const char>()); }
    return joinPath(_root.view(), normalizedPath);
}

}
