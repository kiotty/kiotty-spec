#include "checked.h"
#include "fixed_map.h"
#include "fixed_string.h"
#include "fixed_vector.h"
#include "lazy_pool.h"
#include "result.h"
#include "span.h"
#include "text_buffer.h"
#include "text_diff.h"
#include "utf8.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <utility>

namespace core = kiotty::spec::implementation;

namespace {

using Failure = std::function<bool()>;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

core::Span<const char> textOf(const char* text)
{
    return core::Span<const char>(text, std::strlen(text));
}

bool testResult()
{
    using IntResult = core::Result<int, KiottySpecStatus>;
    const IntResult good = IntResult::success(7);
    const IntResult bad = IntResult::failure(KIOTTY_SPEC_STATUS_OUT_OF_RANGE);
    bool ok = true;
    ok = expectTrue(static_cast<bool>(good), "success is truthy") && ok;
    ok = expectTrue(!bad, "failure is falsy") && ok;
    ok = expectTrue(good.value == 7, "success keeps value") && ok;
    ok = expectTrue(bad.value == 0, "failure value is default") && ok;
    return ok;
}

bool testResultReference()
{
    int target = 3;
    using RefResult = core::Result<int&, KiottySpecStatus>;
    RefResult good = RefResult::success(target);
    RefResult bad = RefResult::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE);
    bool ok = true;
    ok = expectTrue(static_cast<bool>(good) && good.value == &target, "reference success points to target") && ok;
    ok = expectTrue(!bad && bad.value == nullptr, "reference failure is null") && ok;
    return ok;
}

bool testSpanAccess()
{
    char buffer[4] = {'a', 'b', 'c', 'd'};
    const core::Span<char> span(buffer, 4);
    bool ok = true;
    ok = expectTrue(span.length() == 4, "span length") && ok;
    ok = expectTrue(span.at(3).code == KIOTTY_SPEC_STATUS_SUCCESS, "span at last index") && ok;
    ok = expectTrue(span.at(4).code == KIOTTY_SPEC_STATUS_OUT_OF_RANGE, "span at past end fails") && ok;
    return ok;
}

bool testSpanCopy()
{
    char source[3] = {'x', 'y', 'z'};
    char target[4] = {};
    const core::Span<char> destination(target, 4);
    bool ok = true;
    ok = expectTrue(destination.copyFrom(core::Span<const char>(source, 3)) == KIOTTY_SPEC_STATUS_SUCCESS, "copy fits") && ok;
    ok = expectTrue(target[2] == 'z', "copy writes bytes") && ok;
    const core::Span<char> small(target, 2);
    ok = expectTrue(small.copyFrom(core::Span<const char>(source, 3)) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "copy too large fails") && ok;
    return ok;
}

bool testFixedStringAssign()
{
    core::FixedString<5> text;
    bool ok = true;
    ok = expectTrue(text.assign(textOf("abc")) == KIOTTY_SPEC_STATUS_SUCCESS, "assign fits") && ok;
    ok = expectTrue(text.length() == 3 && std::strcmp(text.c_str(), "abc") == 0, "assign stores text") && ok;
    ok = expectTrue(text.append(textOf("de")) == KIOTTY_SPEC_STATUS_SUCCESS, "append up to capacity") && ok;
    ok = expectTrue(std::strcmp(text.c_str(), "abcde") == 0, "append keeps terminator") && ok;
    ok = expectTrue(text.append(textOf("f")) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "append beyond capacity fails") && ok;
    ok = expectTrue(text.length() == 5, "failed append keeps length") && ok;
    return ok;
}

bool testFixedStringOverflowClears()
{
    core::FixedString<2> text;
    text.assign(textOf("ok"));
    const bool failed = text.assign(textOf("too long")) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
    bool ok = true;
    ok = expectTrue(failed, "assign too long fails") && ok;
    ok = expectTrue(text.length() == 0 && text.c_str()[0] == '\0', "failed assign clears") && ok;
    return ok;
}

bool testFixedStringIsCompact()
{
    static_assert(sizeof(core::FixedString<15>) == 16, "no length field");
    static_assert(sizeof(core::FixedString<127>) == 128, "no length field");
    core::FixedString<3> left;
    core::FixedString<3> right;
    left.assign(textOf("ab"));
    right.assign(textOf("ac"));
    bool ok = true;
    ok = expectTrue(left < right && !(right < left), "fixed string orders by text") && ok;
    ok = expectTrue(right.append(textOf("d")) == KIOTTY_SPEC_STATUS_SUCCESS && right.length() == 3, "append fills to capacity") && ok;
    return ok;
}

bool testFixedMap()
{
    using Key = core::FixedString<15>;
    auto keyOf = [](const char* text) {
        Key key;
        key.assign(textOf(text));
        return key;
    };
    core::FixedMap<Key, uint32_t> map(3);
    bool ok = true;
    ok = expectTrue(map.insert(keyOf("REQ-002"), 2) == KIOTTY_SPEC_STATUS_SUCCESS, "insert first") && ok;
    ok = expectTrue(map.insert(keyOf("CR-001"), 0) == KIOTTY_SPEC_STATUS_SUCCESS, "insert before") && ok;
    ok = expectTrue(map.insert(keyOf("DSN-004"), 1) == KIOTTY_SPEC_STATUS_SUCCESS, "insert middle") && ok;
    ok = expectTrue(map.insert(keyOf("DSN-004"), 9) == KIOTTY_SPEC_STATUS_CONFLICT, "duplicate key conflicts") && ok;
    ok = expectTrue(map.insert(keyOf("UT-001"), 3) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "full map rejects") && ok;
    ok = expectTrue(map.keyAt(0) == "CR-001" && map.keyAt(1) == "DSN-004" && map.keyAt(2) == "REQ-002", "keys stay sorted") && ok;
    const auto found = map.find(keyOf("DSN-004"));
    ok = expectTrue(static_cast<bool>(found) && found.value == 1, "find returns value") && ok;
    ok = expectTrue(map.find(keyOf("DSN-005")).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing key not found") && ok;
    ok = expectTrue(map.erase(keyOf("CR-001")) == KIOTTY_SPEC_STATUS_SUCCESS && map.length() == 2, "erase shrinks") && ok;
    ok = expectTrue(map.erase(keyOf("CR-001")) == KIOTTY_SPEC_STATUS_NOT_FOUND, "erase missing") && ok;
    ok = expectTrue(map.keyAt(0) == "DSN-004" && map.find(keyOf("REQ-002")).value == 2, "erase keeps order") && ok;
    return ok;
}

bool testFixedVector()
{
    core::FixedVector<int, 2> values;
    bool ok = true;
    ok = expectTrue(values.append(1) == KIOTTY_SPEC_STATUS_SUCCESS, "first append") && ok;
    ok = expectTrue(values.append(2) == KIOTTY_SPEC_STATUS_SUCCESS, "second append") && ok;
    ok = expectTrue(values.append(3) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "third append fails") && ok;
    ok = expectTrue(values[1] == 2, "index access") && ok;
    ok = expectTrue(values.at(2).code == KIOTTY_SPEC_STATUS_OUT_OF_RANGE, "at past length fails") && ok;
    values.clear();
    ok = expectTrue(values.length() == 0, "clear resets length") && ok;
    return ok;
}

bool testLazyPoolAcquireRelease()
{
    core::LazyPool<int> pool(2);
    const core::Handle first = pool.acquire();
    const core::Handle second = pool.acquire();
    const core::Handle full = pool.acquire();
    bool ok = true;
    ok = expectTrue(first.isValid() && second.isValid(), "two acquires succeed") && ok;
    ok = expectTrue(!full.isValid(), "acquire on full pool is invalid") && ok;
    ok = expectTrue(pool.get(first).code == KIOTTY_SPEC_STATUS_SUCCESS, "live handle resolves") && ok;
    return ok;
}

bool testLazyPoolGenerations()
{
    core::LazyPool<int> pool(1);
    const core::Handle old_handle = pool.acquire();
    *pool.get(old_handle).value = 42;
    bool ok = true;
    ok = expectTrue(pool.release(old_handle) == KIOTTY_SPEC_STATUS_SUCCESS, "release live handle") && ok;
    ok = expectTrue(pool.get(old_handle).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "released handle is invalid") && ok;
    ok = expectTrue(pool.release(old_handle) == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "double release fails") && ok;
    const core::Handle reused = pool.acquire();
    ok = expectTrue(reused.index == old_handle.index && reused.generation != old_handle.generation, "slot reuse bumps generation") && ok;
    ok = expectTrue(pool.get(old_handle).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "stale handle stays invalid after reuse") && ok;
    ok = expectTrue(pool.get(reused).code == KIOTTY_SPEC_STATUS_SUCCESS, "new handle resolves") && ok;
    return ok;
}

bool testTextBuffer()
{
    core::TextBuffer buffer(8);
    bool ok = true;
    ok = expectTrue(buffer.append(textOf("abcd")) == KIOTTY_SPEC_STATUS_SUCCESS, "append fits") && ok;
    ok = expectTrue(buffer.append(textOf("efgh")) == KIOTTY_SPEC_STATUS_SUCCESS, "append fills capacity") && ok;
    ok = expectTrue(buffer.append(textOf("i")) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "append over capacity fails") && ok;
    ok = expectTrue(buffer.view().length() == 8 && std::memcmp(buffer.view().data(), "abcdefgh", 8) == 0, "failed append keeps text") && ok;
    ok = expectTrue(buffer.view().data()[8] == 0, "text stays null terminated") && ok;
    buffer.clear();
    ok = expectTrue(buffer.view().length() == 0 && buffer.capacity() == 8, "clear keeps capacity") && ok;
    return ok;
}

bool testChecked()
{
    const size_t maximum = std::numeric_limits<size_t>::max();
    bool ok = true;
    ok = expectTrue(core::checkedAdd(1, 2).value == 3, "checked add sums") && ok;
    ok = expectTrue(!core::checkedAdd(maximum, 1), "checked add detects overflow") && ok;
    ok = expectTrue(core::checkedMultiply(3, 4).value == 12, "checked multiply product") && ok;
    ok = expectTrue(!core::checkedMultiply(maximum, 2), "checked multiply detects overflow") && ok;
    ok = expectTrue(static_cast<bool>(core::checkedMultiply(0, maximum)), "multiply by zero never overflows") && ok;
    return ok;
}

bool testUtf8Valid()
{
    bool ok = true;
    ok = expectTrue(core::isValidUtf8(textOf("")), "empty is valid") && ok;
    ok = expectTrue(core::isValidUtf8(textOf("abc")), "ascii is valid") && ok;
    ok = expectTrue(core::isValidUtf8(textOf("\xED\x95\x9C\xEA\xB8\x80")), "hangul is valid") && ok;
    ok = expectTrue(core::isValidUtf8(textOf("\xF0\x9F\x98\x80")), "emoji is valid") && ok;
    return ok;
}

bool testUtf8Invalid()
{
    bool ok = true;
    ok = expectTrue(!core::isValidUtf8(textOf("\xC0\xAF")), "overlong two byte rejected") && ok;
    ok = expectTrue(!core::isValidUtf8(textOf("\xE0\x80\x80")), "overlong three byte rejected") && ok;
    ok = expectTrue(!core::isValidUtf8(textOf("\xED\xA0\x80")), "surrogate rejected") && ok;
    ok = expectTrue(!core::isValidUtf8(textOf("\xF4\x90\x80\x80")), "above U+10FFFF rejected") && ok;
    ok = expectTrue(!core::isValidUtf8(textOf("\xE2\x82")), "truncated sequence rejected") && ok;
    ok = expectTrue(!core::isValidUtf8(textOf("\x80")), "stray continuation rejected") && ok;
    return ok;
}

bool expectSegments(const char* before, const char* after, std::initializer_list<std::pair<core::TextChange, const char*>> expected, const char* label)
{
    static uint8_t table[256 * 256];
    core::TextSegment segments[512] {};
    const auto result = core::diffText(textOf(before), textOf(after), core::Span<uint8_t>(table, sizeof(table)), core::Span<core::TextSegment>(segments, 512));
    bool ok = expectTrue(static_cast<bool>(result) && result.value == expected.size(), label);
    size_t index = 0;
    for (const auto& [change, text] : expected) {
        if (!ok || index >= result.value) { break; }
        const core::TextSegment& segment = segments[index++];
        const char* source = segment.source == core::TextSource::BEFORE ? before : after;
        const bool same = segment.change == change && segment.length == std::strlen(text) && std::memcmp(source + segment.offset, text, segment.length) == 0;
        ok = expectTrue(same, label) && ok;
    }
    return ok;
}

bool testTextDiff()
{
    using core::TextChange;
    bool ok = true;
    ok = expectSegments("관계를 링크 타입으로 검사한다", "관계를 모델의 관계 정의로 검사한다",
                        {{TextChange::UNCHANGED, "관계를 "}, {TextChange::REMOVED, "링크 타입으로"}, {TextChange::ADDED, "모델의 관계 정의로"}, {TextChange::UNCHANGED, " 검사한다"}},
                        "design 5.4 summary example") && ok;
    ok = expectSegments("링크 그래프", "관계 저장소", {{TextChange::REMOVED, "링크 그래프"}, {TextChange::ADDED, "관계 저장소"}}, "design 5.4 title example") && ok;
    ok = expectSegments("같은 값", "같은 값", {{TextChange::UNCHANGED, "같은 값"}}, "same value") && ok;
    ok = expectSegments("", "새 요약", {{TextChange::ADDED, "새 요약"}}, "added value") && ok;
    ok = expectSegments("abc def", "abc deg", {{TextChange::UNCHANGED, "abc "}, {TextChange::REMOVED, "def"}, {TextChange::ADDED, "deg"}}, "word aligned") && ok;
    return ok;
}

}

int main()
{
    const std::pair<const char*, Failure> tests[] = {
        {"result", testResult},
        {"result reference", testResultReference},
        {"span access", testSpanAccess},
        {"span copy", testSpanCopy},
        {"fixed string assign", testFixedStringAssign},
        {"fixed string overflow clears", testFixedStringOverflowClears},
        {"fixed string is compact", testFixedStringIsCompact},
        {"fixed map", testFixedMap},
        {"fixed vector", testFixedVector},
        {"lazy pool acquire release", testLazyPoolAcquireRelease},
        {"lazy pool generations", testLazyPoolGenerations},
        {"text buffer", testTextBuffer},
        {"checked arithmetic", testChecked},
        {"utf8 valid", testUtf8Valid},
        {"utf8 invalid", testUtf8Invalid},
        {"text diff", testTextDiff},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool ok = test.second();
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.first);
        if (!ok) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
