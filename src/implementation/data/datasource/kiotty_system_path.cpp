
#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char SEPARATOR[] = "/";

KiottySpecStatus appendSlashed(SystemPath& out, Span<const char> text)
{
    for (size_t index = 0; index < text.length(); ++index) {
        const char letter = platform::isSeparator(text[index]) ? '/' : text[index];
        const KiottySpecStatus appended = out.append(Span<const char>(&letter, 1));
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

SystemPathResult absolutePath(Span<const char> path) { return platform::absolutePath(path); }

SystemPathResult joinPath(Span<const char> base, Span<const char> part)
{
    SystemPath joined {};
    const KiottySpecStatus copied = appendSlashed(joined, base);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    if (part.length() == 0) { return SystemPathResult::success(joined); }
    const bool needs_separator = joined.length() > 0 && !platform::isSeparator(base[base.length() - 1]);
    if (needs_separator) { joined.append(literalText(SEPARATOR)); }
    const KiottySpecStatus appended = appendSlashed(joined, part);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(appended); }
    return SystemPathResult::success(joined);
}

Span<const char> fileNameOf(Span<const char> path)
{
    size_t end = path.length();
    while (end > 0 && platform::isSeparator(path[end - 1])) { --end; }
    size_t begin = end;
    while (begin > 0 && !platform::isSeparator(path[begin - 1])) { --begin; }
    return Span<const char>(path.data() + begin, end - begin);
}

Span<const char> parentOf(Span<const char> path)
{
    size_t index = path.length();
    while (index > 0 && !platform::isSeparator(path[index - 1])) { --index; }
    while (index > 0 && platform::isSeparator(path[index - 1])) { --index; }
    return Span<const char>(path.data(), index);
}

}
