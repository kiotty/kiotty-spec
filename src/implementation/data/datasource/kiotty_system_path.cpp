// 경로 글자 계산. 파일 시스템에는 닿지 않는다 (absolutePath 의 현재 디렉토리 읽기만 예외).

#include "implementation/data/datasource/kiotty_file_system.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/backend/kiotty_platform.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_PATH_PARTS = 256;
constexpr char SEPARATOR[] = "/";
constexpr char CURRENT[] = ".";
constexpr char PARENT[] = "..";

bool sameText(Span<const char> left, Span<const char> right)
{
    if (left.length() != right.length()) { return false; }
    for (size_t index = 0; index < left.length(); ++index) {
        if (!platform::sameChar(left[index], right[index])) { return false; }
    }
    return true;
}

bool isNamed(Span<const char> part, Span<const char> name) { return viewOf(part) == viewOf(name); }

// 구분자를 '/' 로 맞춰 붙인다.
KiottySpecStatus appendSlashed(SystemPath& out, Span<const char> text)
{
    for (size_t index = 0; index < text.length(); ++index) {
        const char letter = platform::isSeparator(text[index]) ? '/' : text[index];
        const KiottySpecStatus appended = out.append(Span<const char>(&letter, 1));
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// "." 는 버리고 ".." 는 앞 부분을 지운다. 루트 위로는 오르지 않는다.
class PartStack
{
public:
    KiottySpecStatus push(Span<const char> part, bool rooted)
    {
        if (part.length() == 0 || isNamed(part, literalText(CURRENT))) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        if (!isNamed(part, literalText(PARENT))) { return keep(part); }
        const bool can_pop = _count > 0 && !isNamed(_parts[_count - 1], literalText(PARENT));
        if (can_pop) {
            --_count;
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        return rooted ? KIOTTY_SPEC_STATUS_SUCCESS : keep(part);
    }

    KiottySpecStatus write(SystemPath& out) const
    {
        for (size_t index = 0; index < _count; ++index) {
            if (index > 0) { out.append(literalText(SEPARATOR)); }
            const KiottySpecStatus appended = out.append(_parts[index]);
            if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
        }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

private:
    KiottySpecStatus keep(Span<const char> part)
    {
        if (_count == MAX_PATH_PARTS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        _parts[_count++] = part;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> _parts[MAX_PATH_PARTS];
    size_t _count {0};
};

KiottySpecStatus splitParts(Span<const char> path, size_t start, bool rooted, PartStack& stack)
{
    size_t begin = start;
    for (size_t index = start; index <= path.length(); ++index) {
        const bool at_end = index == path.length();
        if (!at_end && !platform::isSeparator(path[index])) { continue; }
        const KiottySpecStatus pushed = stack.push(Span<const char>(path.data() + begin, index - begin), rooted);
        if (pushed != KIOTTY_SPEC_STATUS_SUCCESS) { return pushed; }
        begin = index + 1;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}

bool isAbsolutePath(Span<const char> path) { return platform::isAbsolute(path); }

SystemPathResult absolutePath(Span<const char> path) { return platform::absolutePath(path); }

SystemPathResult joinPath(Span<const char> base, Span<const char> part)
{
    SystemPath joined;
    const KiottySpecStatus copied = appendSlashed(joined, base);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(copied); }
    if (part.length() == 0) { return SystemPathResult::success(joined); }
    const bool needs_separator = joined.length() > 0 && !platform::isSeparator(base[base.length() - 1]);
    if (needs_separator) { joined.append(literalText(SEPARATOR)); }
    const KiottySpecStatus appended = appendSlashed(joined, part);
    if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(appended); }
    return SystemPathResult::success(joined);
}

SystemPathResult normalizePath(Span<const char> path)
{
    const size_t root = platform::rootLength(path);
    SystemPath normalized;
    const KiottySpecStatus rooted = appendSlashed(normalized, Span<const char>(path.data(), root));
    if (rooted != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(rooted); }
    PartStack stack;
    const KiottySpecStatus split = splitParts(path, root, root > 0, stack);
    if (split != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(split); }
    const KiottySpecStatus written = stack.write(normalized);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(written); }
    if (normalized.empty()) { normalized.assign(literalText(CURRENT)); }
    return SystemPathResult::success(normalized);
}

SystemPathResult relativeInside(Span<const char> path, Span<const char> root)
{
    const SystemPathResult normalized = normalizePath(path);
    if (!normalized || !isAbsolutePath(path)) { return normalized; }
    const SystemPathResult base = normalizePath(root);
    if (!base) { return SystemPathResult::success(normalized.value); }
    const Span<const char> full = normalized.value.view();
    const Span<const char> prefix = base.value.view();
    if (full.length() < prefix.length() || !sameText(Span<const char>(full.data(), prefix.length()), prefix)) { return normalized; }
    if (full.length() == prefix.length()) { return normalizePath(literalText(CURRENT)); }
    const bool base_ends_with_separator = prefix.length() > 0 && prefix[prefix.length() - 1] == '/';
    const size_t start = base_ends_with_separator ? prefix.length() : prefix.length() + 1;
    const bool at_boundary = base_ends_with_separator || full[prefix.length()] == '/';
    if (!at_boundary) { return normalized; }
    return normalizePath(Span<const char>(full.data() + start, full.length() - start));
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
