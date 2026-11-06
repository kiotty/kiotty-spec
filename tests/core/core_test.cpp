#include "checked.h"
#include "fixed_string.h"
#include "fixed_vector.h"
#include "lazy_pool.h"
#include "result.h"
#include "span.h"
#include "utf8.h"

#include <cstdio>
#include <cstring>
#include <functional>
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
        {"fixed vector", testFixedVector},
        {"lazy pool acquire release", testLazyPoolAcquireRelease},
        {"lazy pool generations", testLazyPoolGenerations},
        {"checked arithmetic", testChecked},
        {"utf8 valid", testUtf8Valid},
        {"utf8 invalid", testUtf8Invalid},
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
