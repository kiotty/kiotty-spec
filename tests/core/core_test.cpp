#include "kiotty_checked.h"
#include "kiotty_fixed_map.h"
#include "kiotty_fixed_string.h"
#include "kiotty_fixed_vector.h"
#include "kiotty_json_reader.h"
#include "kiotty_json_writer.h"
#include "kiotty_lazy_pool.h"
#include "kiotty_result.h"
#include "kiotty_search.h"
#include "kiotty_span.h"
#include "kiotty_span_text.h"
#include "kiotty_text_buffer.h"
#include "kiotty_text_diff.h"
#include "kiotty_utf8.h"

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

bool sameText(core::Span<const char> left, const char* right)
{
    return left.length() == std::strlen(right) && std::memcmp(left.data(), right, left.length()) == 0;
}

bool testJsonWriterString()
{
    core::TextBuffer text(256);
    bool ok = expectTrue(core::appendJsonString(text, textOf("a\"b\\c\n\t/\x01\x7f")) == KIOTTY_SPEC_STATUS_SUCCESS, "escape string");
    ok = expectTrue(sameText(text.view(), "\"a\\\"b\\\\c\\n\\t/\\u0001\x7f\""), "escaped text") && ok;
    text.clear();
    core::appendJsonString(text, textOf("\xED\x95\x9C" "x\xFF" "y\xE0\x80"));
    ok = expectTrue(sameText(text.view(), "\"\xED\x95\x9C" "x\xEF\xBF\xBD" "y\xEF\xBF\xBD\xEF\xBF\xBD\""), "invalid utf8 is replaced per byte") && ok;
    return ok;
}

bool testJsonWriterNumbers()
{
    core::TextBuffer text(64);
    core::appendJsonInteger(text, -42);
    text.append(textOf(","));
    core::appendJsonInteger(text, std::numeric_limits<int64_t>::min());
    text.append(textOf(","));
    core::appendJsonReal(text, 1.0);
    text.append(textOf(","));
    core::appendJsonReal(text, 0.5);
    return expectTrue(sameText(text.view(), "-42,-9223372036854775808,1.0,0.5"), "numbers");
}

bool testJsonReaderFields()
{
    char scratch[64] {};
    const core::Span<char> area(scratch, sizeof(scratch));
    core::JsonReader reader(textOf(" {\"a\":1, \"b\":\"x\\u00e9\\ud83d\\ude00\\n\",\"c\":null,\"d\":[ [1, 4] ],\"e\":true,\"f\":-2.5,\"g\":18446744073709551615} "));
    core::JsonField field;
    bool ok = true;
    auto next = reader.next(field, area);
    ok = expectTrue(next.value && sameText(field.key, "a") && field.type == core::JsonType::INTEGER && field.integer == 1, "integer field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::TEXT && sameText(field.text, "x\xC3\xA9\xF0\x9F\x98\x80\n"), "escaped text field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::NULL_VALUE, "null field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::COMPOSITE && sameText(field.text, "[[1,4]]"), "array is compacted") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::BOOLEAN && field.boolean, "boolean field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::REAL && field.real == -2.5, "real field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next.value && field.type == core::JsonType::UNSIGNED && field.natural == std::numeric_limits<uint64_t>::max(), "unsigned field") && ok;
    next = reader.next(field, area);
    ok = expectTrue(next && !next.value, "end of object") && ok;
    return ok;
}

bool testJsonReaderFindSkipsOtherValues()
{
    // 찾는 키 앞의 긴 값은 작은 scratch 에 풀지 않고 건너뛴다.
    char scratch[4] {};
    core::JsonReader reader(textOf("{\"body\":\"long text \\\" here\",\"list\":[\"]\"],\"id\":\"A1\"}"));
    core::JsonField field;
    const auto found = reader.find(textOf("id"), field, core::Span<char>(scratch, sizeof(scratch)));
    bool ok = expectTrue(found && found.value && sameText(field.text, "A1"), "find skips long values");
    const auto missing = reader.find(textOf("nope"), field, core::Span<char>(scratch, sizeof(scratch)));
    ok = expectTrue(missing && !missing.value, "missing key") && ok;
    const auto small = reader.find(textOf("body"), field, core::Span<char>(scratch, sizeof(scratch)));
    ok = expectTrue(small.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "wanted value larger than scratch") && ok;
    return ok;
}

bool testJsonReaderRejects()
{
    char scratch[16] {};
    const core::Span<char> area(scratch, sizeof(scratch));
    const char* const broken[] = {"[1]", "{\"a\":1,}", "{\"a\" 1}", "{\"a\":01}", "{\"a\":\"x}", "{\"a\":1} x", "{\"a\":\"\\q\"}", "{\"a\":\"\\udc00\"}", "{\"a\":tru}"};
    bool ok = true;
    for (const char* line : broken) {
        core::JsonReader reader(textOf(line));
        core::JsonField field;
        bool failed = false;
        for (int guard = 0; guard < 4; ++guard) {
            const auto next = reader.next(field, area);
            if (!next) {
                failed = true;
                break;
            }
            if (!next.value) { break; }
        }
        ok = expectTrue(failed, line) && ok;
    }
    core::JsonReader empty(textOf("{ }"));
    core::JsonField field;
    const auto none = empty.next(field, area);
    ok = expectTrue(none && !none.value, "empty object") && ok;
    return ok;
}

bool testSpanText()
{
    char area[8] {};
    core::SpanText text(core::Span<char>(area, sizeof(area)));
    bool ok = expectTrue(text.append(textOf("abc")) == KIOTTY_SPEC_STATUS_SUCCESS && text.capacity() == 7, "span text append");
    ok = expectTrue(text.append(textOf("12345")) == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED && sameText(text.view(), "abc"), "span text keeps on overflow") && ok;
    ok = expectTrue(area[3] == '\0', "span text is terminated") && ok;
    return ok;
}


bool testSearch()
{
    const int values[] = {1, 2, 2, 2, 3, 2};
    const core::Span<const int> all(values, 5);
    const auto twos = core::contiguousRange(all, [](int value) { return value == 2; });
    bool ok = expectTrue(twos && twos.value.first == 1 && twos.value.length == 3, "contiguous range");
    const auto none = core::contiguousRange(all, [](int value) { return value == 9; });
    ok = expectTrue(none && none.value.length == 0, "empty range") && ok;
    const auto split = core::contiguousRange(core::Span<const int>(values, 6), [](int value) { return value == 2; });
    ok = expectTrue(split.code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "split range is rejected") && ok;
    const auto three = core::findIndex(all, [](int value) { return value == 3; });
    ok = expectTrue(three && three.value == 4, "find index") && ok;
    ok = expectTrue(core::findIndex(all, [](int value) { return value == 9; }).code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing index") && ok;
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
        {"json writer string", testJsonWriterString},
        {"json writer numbers", testJsonWriterNumbers},
        {"json reader fields", testJsonReaderFields},
        {"json reader find skips", testJsonReaderFindSkipsOtherValues},
        {"json reader rejects", testJsonReaderRejects},
        {"span text", testSpanText},
        {"search", testSearch},
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
