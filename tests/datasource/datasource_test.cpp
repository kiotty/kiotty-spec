#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"

#include <cstdio>
#include <string_view>
#include <utility>

namespace core = kiotty::spec::implementation;

namespace {

using Test = bool (*)();

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool sameText(core::Span<const char> left, std::string_view right) { return core::viewOf(left) == right; }

bool testTextView()
{
    const char text[] = "abcdef";
    bool ok = true;
    ok = expectTrue(core::boundedText(text, 3).length() == 3, "bounded text stops at limit") && ok;
    ok = expectTrue(core::boundedText(text, 100).length() == 6, "bounded text stops at terminator") && ok;
    ok = expectTrue(core::boundedText(nullptr, 5).length() == 0, "bounded text of null is empty") && ok;
    ok = expectTrue(core::literalText("xyz").length() == 3, "literal text drops terminator") && ok;
    return ok;
}

bool testFileReaderLines()
{
    const core::Span<const char> path = core::literalText("kiotty_file_reader_test.txt");
    bool ok = true;
    {
        core::FileWriter writer;
        ok = expectTrue(writer.open(path) == KIOTTY_SPEC_STATUS_SUCCESS, "open writer") && ok;
        writer.write(core::literalText("first\r\n\nsecond  \n"));
        writer.write(core::literalText("third-without-newline"));
    }
    {
        char buffer[24] {};
        core::FileReader reader(core::Span<char>(buffer, sizeof(buffer)));
        ok = expectTrue(reader.open(path) == KIOTTY_SPEC_STATUS_SUCCESS, "open reader") && ok;
        const char* const expected[] = {"first", "", "second  ", "third-without-newline"};
        for (const char* line : expected) {
            const auto next = reader.next();
            ok = expectTrue(next && next.value && sameText(reader.line(), line), line) && ok;
        }
        const auto end = reader.next();
        ok = expectTrue(end && !end.value, "end of file") && ok;

        char small[8] {};
        core::FileReader tight(core::Span<char>(small, sizeof(small)));
        tight.open(path);
        tight.next();
        tight.next();
        const auto too_long = tight.next();
        ok = expectTrue(too_long.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "line longer than buffer") && ok;

        core::FileReader missing(core::Span<char>(buffer, sizeof(buffer)));
        ok = expectTrue(missing.open(core::literalText("no_such_kiotty_file.txt")) == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing file") && ok;
    }
    core::removeFile(path);
    return ok;
}

bool testLineRoundTrip()
{
    char line_area[64] {};
    core::KispLineEncoder encoder(core::Span<char>(line_area, sizeof(line_area)));
    encoder.add(core::literalText("item"));
    encoder.add(core::literalText("a\tb\nc\\d"));
    encoder.add(core::Span<const char>());
    encoder.add(core::literalText("끝 "));
    const auto line = encoder.finish();
    bool ok = expectTrue(line && sameText(line.value, "item\ta\\tb\\nc\\\\d\t\t끝 \n"), "encode escapes tab newline backslash");
    char field_area[64] {};
    const core::KispLineDecoder decoder(core::Span<char>(field_area, sizeof(field_area)));
    const auto fields = decoder.decode(core::Span<const char>(line.value.data(), line.value.length() - 1));
    ok = expectTrue(fields && fields.value.count == 4, "four fields") && ok;
    ok = expectTrue(sameText(fields.value.values[1], "a\tb\nc\\d") && fields.value.values[2].length() == 0, "decode restores") && ok;
    ok = expectTrue(sameText(fields.value.values[3], "끝 "), "trailing space kept") && ok;
    return ok;
}

// 개발수칙 2.10: 빈 입력, 최대 길이, 최대 길이 + 1, 중간 null 바이트, 잘못된 UTF-8
bool testLineDecoderInputs()
{
    char area[8] {};
    const core::KispLineDecoder decoder(core::Span<char>(area, sizeof(area)));
    const auto empty = decoder.decode(core::Span<const char>());
    bool ok = expectTrue(empty && empty.value.count == 1 && empty.value.values[0].length() == 0, "empty line is one empty field");
    ok = expectTrue(static_cast<bool>(decoder.decode(core::literalText("1234\t5678"))), "longest line fits") && ok;
    ok = expectTrue(decoder.decode(core::literalText("1234\t56789")).code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "one byte more") && ok;
    const char with_null[] = {'a', '\0', 'b'};
    const auto nulled = decoder.decode(core::Span<const char>(with_null, sizeof(with_null)));
    ok = expectTrue(nulled && nulled.value.values[0].length() == 3, "null byte passes to the parser") && ok;
    const auto broken = decoder.decode(core::literalText("\xff\xfe"));
    ok = expectTrue(broken && broken.value.values[0].length() == 2, "utf-8 is checked by the parser") && ok;
    ok = expectTrue(decoder.decode(core::literalText("a\\q")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "unknown escape") && ok;
    ok = expectTrue(decoder.decode(core::literalText("a\\")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "dangling escape") && ok;
    char wide[64] {};
    const core::KispLineDecoder roomy(core::Span<char>(wide, sizeof(wide)));
    ok = expectTrue(roomy.decode(core::literalText("1\t2\t3\t4\t5\t6\t7\t8\t9\t10\t11")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "too many fields") && ok;
    return ok;
}

bool testKispFileWriteAndHash()
{
    const core::Span<const char> path = core::literalText("kiotty_kisp_test.kisp");
    core::removeFile(path);
    char buffer[64] {};
    bool ok = expectTrue(core::hashKispFile(path, core::Span<char>(buffer, sizeof(buffer))).value == core::EMPTY_KISP_HASH, "missing file hash");
    uint64_t written = 0;
    {
        core::KispFileWriter writer;
        ok = expectTrue(writer.open(path) == KIOTTY_SPEC_STATUS_SUCCESS, "open kisp writer") && ok;
        writer.write(core::literalText("#kisp 1 model 1\n"));
        writer.write(core::literalText("meta\tO\tproject.name\tx\n"));
        ok = expectTrue(!core::isFile(path), "nothing replaced before commit") && ok;
        ok = expectTrue(writer.commit() == KIOTTY_SPEC_STATUS_SUCCESS, "commit") && ok;
        written = writer.hash();
    }
    ok = expectTrue(core::hashKispFile(path, core::Span<char>(buffer, sizeof(buffer))).value == written, "reader hash matches writer hash") && ok;
    {
        core::KispFileWriter abandoned;
        abandoned.open(path);
        abandoned.write(core::literalText("partial"));
    }
    ok = expectTrue(core::hashKispFile(path, core::Span<char>(buffer, sizeof(buffer))).value == written, "abandoned write leaves file") && ok;
    ok = expectTrue(!core::isFile(core::literalText("kiotty_kisp_test.kisp.tmp")), "temporary file removed") && ok;
    {
        char line_area[64] {};
        core::KispFileReader reader(core::Span<char>(buffer, sizeof(buffer)), core::Span<char>(line_area, sizeof(line_area)));
        reader.open(path);
        reader.next();
        reader.next();
        const auto fields = reader.fields();
        ok = expectTrue(fields && fields.value.count == 4 && reader.lineNumber() == 2 && sameText(fields.value.values[3], "x"), "read fields") && ok;
    }
    core::removeFile(path);
    return ok;
}

}

int main()
{
    const std::pair<const char*, Test> tests[] = {
        {"text view", testTextView},
        {"file reader lines", testFileReaderLines},
        {"line round trip", testLineRoundTrip},
        {"line decoder inputs", testLineDecoderInputs},
        {"kisp file write and hash", testKispFileWriteAndHash},
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
