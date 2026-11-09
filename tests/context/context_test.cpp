#include "implementation/context/cursor_pool.h"

#include <cstdio>
#include <functional>
#include <utility>

namespace impl = kiotty::spec::implementation;

namespace {

using Check = std::function<bool()>;

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

impl::CursorRow rowOf(int64_t number)
{
    impl::CursorRow row;
    row.number = number;
    return row;
}

bool testCursorPages()
{
    impl::CursorPool pool(2, 5);
    const auto cursor = pool.open(impl::CursorKind::ITEM);
    bool ok = expectTrue(static_cast<bool>(cursor) && cursor.value != 0, "open gives non-zero id");
    for (int64_t number = 0; number < 5; ++number) { pool.push(cursor.value, rowOf(number)); }
    ok = expectTrue(pool.push(cursor.value, rowOf(5)) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "push over row capacity fails") && ok;
    ok = expectTrue(pool.total(cursor.value).value == 5, "total counts rows") && ok;
    const auto first = pool.fetch(cursor.value, impl::CursorKind::ITEM, 3);
    ok = expectTrue(first.value.length() == 3 && first.value[0].number == 0 && first.value[2].number == 2, "first page") && ok;
    const auto second = pool.fetch(cursor.value, impl::CursorKind::ITEM, 3);
    ok = expectTrue(second.value.length() == 2 && second.value[0].number == 3, "second page is the rest") && ok;
    const auto done = pool.fetch(cursor.value, impl::CursorKind::ITEM, 3);
    ok = expectTrue(static_cast<bool>(done) && done.value.length() == 0, "empty page at end") && ok;
    return ok;
}

bool testCursorKindAndHandles()
{
    impl::CursorPool pool(1, 4);
    const auto cursor = pool.open(impl::CursorKind::LINK);
    bool ok = expectTrue(pool.open(impl::CursorKind::ITEM).code == KIOTTY_SPEC_STATUS_POOL_IS_FULL, "pool full");
    ok = expectTrue(pool.fetch(cursor.value, impl::CursorKind::ITEM, 1).code == KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "wrong kind") && ok;
    ok = expectTrue(pool.close(cursor.value) == KIOTTY_SPEC_STATUS_SUCCESS, "close") && ok;
    ok = expectTrue(pool.close(cursor.value) == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "double close") && ok;
    const auto reused = pool.open(impl::CursorKind::ITEM);
    ok = expectTrue(static_cast<bool>(reused) && reused.value != cursor.value, "reuse gives new id") && ok;
    ok = expectTrue(pool.total(cursor.value).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "stale id rejected") && ok;
    ok = expectTrue(pool.total(0).code == KIOTTY_SPEC_STATUS_INVALID_HANDLE, "zero id rejected") && ok;
    ok = expectTrue(pool.total(reused.value).value == 0, "reused cursor starts empty") && ok;
    return ok;
}

}

int main()
{
    const std::pair<const char*, Check> tests[] = {
        {"cursor pages", testCursorPages},
        {"cursor kind and handles", testCursorKindAndHandles},
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
