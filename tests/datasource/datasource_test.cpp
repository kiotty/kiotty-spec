#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/usecases/spec_file/kiotty_spec_fields.h"

#include <cstdio>
#include <filesystem>
#include <string>
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

core::PathArea paths {};
const core::FileSystem file_system(paths);

bool testFileReaderLines()
{
    const core::Span<const char> path = core::literalText("kiotty_file_reader_test.txt");
    bool ok = true;
    {
        core::OpenedFileResult opened = core::OpenedFile::openForWriting(file_system, path);
        ok = expectTrue(static_cast<bool>(opened), "open writer") && ok;
        core::FileWriter writer(std::move(opened.value));
        writer.write(core::literalText("first\r\n\nsecond  \n"));
        writer.write(core::literalText("third-without-newline"));
    }
    {
        char buffer[24] {};
        core::OpenedFileResult opened = core::OpenedFile::openForReading(file_system, path);
        ok = expectTrue(static_cast<bool>(opened), "open reader") && ok;
        core::FileReader reader(std::move(opened.value), core::Span<char>(buffer, sizeof(buffer)));
        const char* const expected[] = {"first", "", "second  ", "third-without-newline"};
        for (const char* line : expected) {
            const auto next = reader.next();
            ok = expectTrue(next && next.value && sameText(reader.line(), line), line) && ok;
        }
        const auto end = reader.next();
        ok = expectTrue(end && !end.value, "end of file") && ok;

        char small[8] {};
        core::FileReader tight(std::move(core::OpenedFile::openForReading(file_system, path).value), core::Span<char>(small, sizeof(small)));
        tight.next();
        tight.next();
        const auto too_long = tight.next();
        ok = expectTrue(too_long.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "line longer than buffer") && ok;

        const core::OpenedFileResult missing = core::OpenedFile::openForReading(file_system, core::literalText("no_such_kiotty_file.txt"));
        ok = expectTrue(missing.code == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing file") && ok;
    }
    file_system.removeFile(path);
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
    ok = expectTrue(core::readText(nulled.value, 0, 8).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "parser rejects null byte") && ok;
    const auto broken = decoder.decode(core::literalText("\xff\xfe"));
    ok = expectTrue(broken && broken.value.values[0].length() == 2, "utf-8 is checked by the parser") && ok;
    ok = expectTrue(core::readText(broken.value, 0, 8).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "parser rejects invalid utf-8") && ok;
    ok = expectTrue(decoder.decode(core::literalText("a\\q")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "unknown escape") && ok;
    ok = expectTrue(decoder.decode(core::literalText("a\\")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "dangling escape") && ok;
    char wide[64] {};
    const core::KispLineDecoder roomy(core::Span<char>(wide, sizeof(wide)));
    ok = expectTrue(roomy.decode(core::literalText("1\t2\t3\t4\t5\t6\t7\t8\t9\t10\t11")).code == KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "too many fields") && ok;
    return ok;
}

core::KispFields fieldsOf(core::Span<const char> value)
{
    core::KispFields fields {};
    fields.values[0] = value;
    fields.count = 1;
    return fields;
}

// 개발수칙 2.10: 빈 입력, 최대 길이, 최대 길이 + 1, 중간 null 바이트, 잘못된 UTF-8
struct FieldInputs {
    core::Span<const char> longest;
    core::Span<const char> too_long;
    core::Span<const char> with_null;
    core::Span<const char> broken;
};

template <typename Parse>
bool checkFieldInputs(const char* name, const FieldInputs& inputs, Parse parse)
{
    const std::string prefix(name);
    bool ok = expectTrue(!parse(fieldsOf(core::Span<const char>())), (prefix + " empty").c_str());
    ok = expectTrue(static_cast<bool>(parse(fieldsOf(inputs.longest))), (prefix + " max length").c_str()) && ok;
    ok = expectTrue(!parse(fieldsOf(inputs.too_long)), (prefix + " max length + 1").c_str()) && ok;
    ok = expectTrue(!parse(fieldsOf(inputs.with_null)), (prefix + " null byte").c_str()) && ok;
    ok = expectTrue(!parse(fieldsOf(inputs.broken)), (prefix + " invalid utf-8").c_str()) && ok;
    return ok;
}

bool testFieldParserInputs()
{
    const FieldInputs key {core::literalText("0000000a"), core::literalText("00000000a"), core::literalText("0000000\0"),
                           core::literalText("0000000\xff")};
    bool ok = checkFieldInputs("key", key, [](const core::KispFields& fields) { return core::parseKey(fields, 0); });
    ok = expectTrue(core::parseKey(fieldsOf(key.longest), 0).value == 10, "key value") && ok;
    ok = expectTrue(!core::parseKey(fieldsOf(core::literalText("00000000")), 0), "no key is rejected") && ok;
    ok = checkFieldInputs("optional key", key, [](const core::KispFields& fields) { return core::parseOptionalKey(fields, 0); }) && ok;
    ok = expectTrue(core::parseOptionalKey(fieldsOf(core::literalText("-")), 0).value == 0, "optional key none") && ok;
    const FieldInputs sequence {core::literalText("4294967295"), core::literalText("4294967296"), core::literalText("1\0"),
                                core::literalText("1\xff")};
    ok = checkFieldInputs("sequence", sequence, [](const core::KispFields& fields) { return core::parseSequence(fields, 0); }) && ok;
    ok = expectTrue(!core::parseSequence(fieldsOf(core::literalText("0")), 0), "sequence zero") && ok;
    const FieldInputs number {core::literalText("99"), core::literalText("100"), core::literalText("9\0"), core::literalText("9\xff")};
    ok = checkFieldInputs("number", number, [](const core::KispFields& fields) { return core::parseNumber(fields, 0, 99); }) && ok;
    const FieldInputs hash {core::literalText("0123456789abcdef"), core::literalText("0123456789abcdef0"),
                            core::literalText("0123456789abcde\0"), core::literalText("0123456789abcde\xff")};
    ok = checkFieldInputs("hash", hash, [](const core::KispFields& fields) { return core::parseHash(fields, 0); }) && ok;
    ok = expectTrue(core::parseHash(fieldsOf(core::literalText("-")), 0).value == 0, "hash none") && ok;
    const FieldInputs kind {core::literalText("code_inspection_result"), core::literalText("code_inspection_resultx"),
                            core::literalText("design\0"), core::literalText("design\xff")};
    ok = checkFieldInputs("kind name", kind, [](const core::KispFields& fields) { return core::parseKindName(fields, 0); }) && ok;
    ok = expectTrue(static_cast<bool>(core::readText(fieldsOf(core::Span<const char>()), 0, 4)), "text empty") && ok;
    ok = expectTrue(static_cast<bool>(core::readText(fieldsOf(core::literalText("abcd")), 0, 4)), "text max length") && ok;
    ok = expectTrue(!core::readText(fieldsOf(core::literalText("abcde")), 0, 4), "text max length + 1") && ok;
    ok = expectTrue(!core::readText(fieldsOf(core::literalText("ab\0d")), 0, 4), "text null byte") && ok;
    ok = expectTrue(!core::readText(fieldsOf(core::literalText("ab\xff")), 0, 4), "text invalid utf-8") && ok;
    ok = expectTrue(!core::parseKey(core::KispFields {}, 0), "missing field") && ok;
    return ok;
}

// 개발수칙 6.4: 빈 경로와 4 바이트 UTF-8 경로가 같은 변환 루프를 지난다.
bool testUnicodePath()
{
    const core::Span<const char> path = core::literalText("kiotty_경로_\xF0\x9F\x98\x80.txt");
    bool ok = true;
    {
        core::OpenedFileResult opened = core::OpenedFile::openForWriting(file_system, path);
        ok = expectTrue(static_cast<bool>(opened), "open unicode writer") && ok;
        core::FileWriter writer(std::move(opened.value));
        writer.write(core::literalText("x"));
    }
    ok = expectTrue(file_system.isFile(path), "unicode file exists") && ok;
    std::error_code error;
    const bool is_native = std::filesystem::exists(std::filesystem::u8path(core::viewOf(path)), error);
    ok = expectTrue(is_native, "unicode file has the native name") && ok;
    ok = expectTrue(file_system.removeFile(path) == KIOTTY_SPEC_STATUS_SUCCESS && !file_system.isFile(path), "unicode file removed") && ok;
    ok = expectTrue(!file_system.isFile(core::Span<const char>()), "empty path is no file") && ok;
    ok = expectTrue(file_system.openForReading(core::literalText("bad\xff")).code == KIOTTY_SPEC_STATUS_INVALID_UTF8, "invalid utf-8 path") && ok;
    return ok;
}

bool testKispFileWriteAndHash()
{
    const core::Span<const char> path = core::literalText("kiotty_kisp_test.kisp");
    file_system.removeFile(path);
    char buffer[64] {};
    bool ok = expectTrue(core::hashKispFile(file_system, path, core::Span<char>(buffer, sizeof(buffer))).value == core::EMPTY_KISP_HASH, "missing file hash");
    uint64_t written = 0;
    core::SystemPath temporary_path {};
    temporary_path.assign(path);
    temporary_path.append(core::literalText(core::KISP_TEMPORARY_SUFFIX));
    {
        core::OpenedFileResult opened = core::OpenedFile::openForWriting(file_system, temporary_path.view());
        ok = expectTrue(static_cast<bool>(opened), "open kisp writer") && ok;
        core::KispFileWriter writer(file_system, std::move(opened.value), path, temporary_path.view());
        writer.write(core::literalText("#kisp 2 model 1\n"));
        writer.write(core::literalText("meta\tO\tproject.name\tx\n"));
        ok = expectTrue(!file_system.isFile(path), "nothing replaced before commit") && ok;
        ok = expectTrue(writer.commit() == KIOTTY_SPEC_STATUS_SUCCESS, "commit") && ok;
        written = writer.hash();
    }
    ok = expectTrue(core::hashKispFile(file_system, path, core::Span<char>(buffer, sizeof(buffer))).value == written, "reader hash matches writer hash") && ok;
    {
        core::OpenedFileResult opened = core::OpenedFile::openForWriting(file_system, temporary_path.view());
        core::KispFileWriter abandoned(file_system, std::move(opened.value), path, temporary_path.view());
        abandoned.write(core::literalText("partial"));
    }
    ok = expectTrue(core::hashKispFile(file_system, path, core::Span<char>(buffer, sizeof(buffer))).value == written, "abandoned write leaves file") && ok;
    ok = expectTrue(!file_system.isFile(core::literalText("kiotty_kisp_test.kisp.tmp")), "temporary file removed") && ok;
    {
        char line_area[64] {};
        core::OpenedFileResult opened = core::openKispFile(file_system, path);
        core::KispFileReader reader(std::move(opened.value), core::Span<char>(buffer, sizeof(buffer)), core::Span<char>(line_area, sizeof(line_area)));
        reader.next();
        reader.next();
        const auto fields = reader.fields();
        ok = expectTrue(fields && fields.value.count == 4 && reader.lineNumber() == 2 && sameText(fields.value.values[3], "x"), "read fields") && ok;
    }
    file_system.removeFile(path);
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
        {"field parser inputs", testFieldParserInputs},
        {"unicode path", testUnicodePath},
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
