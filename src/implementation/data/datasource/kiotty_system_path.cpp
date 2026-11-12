
#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char SEPARATOR[] = "/";

class SlashedPathWriter
{
public:
    explicit SlashedPathWriter(SystemPath& path) : _path(path) {}

    KiottySpecStatus appendSlashed(Span<const char> text);

private:
    SystemPath& _path;
};

bool isSeparatorAt(Span<const char> path, size_t index);

}

SystemPathResult absolutePath(Span<const char> path) { return platform::absolutePath(path); }

SystemPathResult joinPath(Span<const char> base, Span<const char> part)
{
    SystemPath joined {};
    SlashedPathWriter writer(joined);
    const KiottySpecStatus copied = writer.appendSlashed(base);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    const size_t part_length = part.length();
    if (part_length == 0) { return SystemPathResult::success(joined); }
    const bool has_trailing_separator = isSeparatorAt(base, base.length() - 1);
    const bool is_separator_needed = joined.length() > 0 && !has_trailing_separator;
    if (is_separator_needed) { joined.append(literalText(SEPARATOR)); }
    const KiottySpecStatus appended = writer.appendSlashed(part);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(appended); }
    return SystemPathResult::success(joined);
}

Span<const char> fileNameOf(Span<const char> path)
{
    size_t end = path.length();
    while (end > 0) {
        const bool is_separator = isSeparatorAt(path, end - 1);
        if (!is_separator) { break; }
        --end;
    }
    size_t begin = end;
    while (begin > 0) {
        const bool is_separator = isSeparatorAt(path, begin - 1);
        if (is_separator) { break; }
        --begin;
    }
    return Span<const char>(path.data() + begin, end - begin);
}

Span<const char> parentOf(Span<const char> path)
{
    size_t index = path.length();
    while (index > 0) {
        const bool is_separator = isSeparatorAt(path, index - 1);
        if (is_separator) { break; }
        --index;
    }
    while (index > 0) {
        const bool is_separator = isSeparatorAt(path, index - 1);
        if (!is_separator) { break; }
        --index;
    }
    return Span<const char>(path.data(), index);
}

namespace {

KiottySpecStatus SlashedPathWriter::appendSlashed(Span<const char> text)
{
    const size_t length = text.length();
    for (size_t index = 0; index < length; ++index) {
        const char letter = platform::isSeparator(text[index]) ? '/' : text[index];
        const KiottySpecStatus appended = _path.append(Span<const char>(&letter, 1));
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

bool isSeparatorAt(Span<const char> path, size_t index)
{
    const Result<const char&, KiottySpecStatus> letter = path.at(index);
    return letter && platform::isSeparator(*letter.value);
}

}

}
