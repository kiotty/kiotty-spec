#ifdef _WIN32

#include "implementation/data/infra/platform/kiotty_platform.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"

#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cwchar>

namespace kiotty::spec::implementation {

namespace platform {

namespace {

using FileStatus = struct _stat64;
using ModeResult = Result<uint32_t, KiottySpecStatus>;
using WrittenResult = Result<size_t, KiottySpecStatus>;

constexpr size_t DOUBLE_SEPARATOR_ROOT_LENGTH = 2;
constexpr size_t DRIVE_LENGTH = 2;
constexpr size_t DRIVE_ROOT_LENGTH = 3;
constexpr size_t SURROGATE_UNITS = 2;
constexpr uint32_t HIGH_SURROGATE_BASE = 0xD800;
constexpr uint32_t LOW_SURROGATE_BASE = 0xDC00;
constexpr uint32_t SURROGATE_PAYLOAD_BITS = 10;
constexpr uint32_t SURROGATE_PAYLOAD_MASK = 0x3FFu;

ModeResult readMode(const NativePath& path);
WrittenResult writeCodePoint(Span<wchar_t> native, size_t offset, uint32_t point);
KiottySpecStatus writeTerminator(Span<wchar_t> native, size_t length);
bool isLetter(char letter);
char readLetter(Span<const char> path, size_t index);

}

}

namespace {

constexpr size_t UTF8_SEQUENCE_LENGTH = 4;
constexpr wchar_t HIGH_SURROGATE_FIRST = 0xD800;
constexpr wchar_t HIGH_SURROGATE_LAST = 0xDBFF;
constexpr size_t SURROGATE_PAIR_LENGTH = 2;

class NarrowPathWriter
{
public:
    explicit NarrowPathWriter(SystemPath& path) : _path(path) {}

    KiottySpecStatus appendNarrowed(Span<const wchar_t> wide);

private:
    KiottySpecStatus appendCodePoint(Span<const wchar_t> code_point);

    SystemPath& _path;
};

size_t measureCodePoint(Span<const wchar_t> wide, size_t index);

}

SystemPathResult PathResolver::resolveAbsolutePath(Span<const char> path) const
{
    const KiottySpecStatus converted = platform::convertPath(path, _area.source.letters());
    if (converted != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(converted); }
    const wchar_t* const resolved =
        _wfullpath(_area.target.text, _area.source.text, MAX_SYSTEM_PATH_LENGTH);
    if (resolved == nullptr) {
        return SystemPathResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT);
    }
    _area.absolute.clear();
    const size_t length = wcsnlen(_area.target.text, MAX_SYSTEM_PATH_LENGTH);
    NarrowPathWriter writer(_area.absolute);
    const KiottySpecStatus narrowed =
        writer.appendNarrowed(Span<const wchar_t>(_area.target.text, length));
    if (narrowed != KIOTTY_SPEC_STATUS_SUCCESS) { return SystemPathResult::failure(narrowed); }
    return SystemPathResult::success(_area.absolute);
}

namespace {

KiottySpecStatus NarrowPathWriter::appendNarrowed(Span<const wchar_t> wide)
{
    size_t index = 0;
    const size_t length = wide.length();
    while (index < length) {
        const size_t units = measureCodePoint(wide, index);
        const KiottySpecStatus appended =
            appendCodePoint(Span<const wchar_t>(wide.data() + index, units));
        if (appended != KIOTTY_SPEC_STATUS_SUCCESS) { return appended; }
        index += units;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus NarrowPathWriter::appendCodePoint(Span<const wchar_t> code_point)
{
    char letters[UTF8_SEQUENCE_LENGTH] {};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, code_point.data(), static_cast<int>(code_point.length()),
                            letters, static_cast<int>(UTF8_SEQUENCE_LENGTH), nullptr, nullptr);
    if (length <= 0) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const Span<const char> narrowed(letters, static_cast<size_t>(length));
    const char first = platform::readLetter(narrowed, 0);
    const bool is_separator = length == 1 && platform::isSeparator(first);
    return _path.append(is_separator ? literalText("/") : narrowed);
}

size_t measureCodePoint(Span<const wchar_t> wide, size_t index)
{
    const Result<const wchar_t&, KiottySpecStatus> first = wide.at(index);
    const bool has_next = index + 1 < wide.length();
    const bool is_high_surrogate =
        first && *first.value >= HIGH_SURROGATE_FIRST && *first.value <= HIGH_SURROGATE_LAST;
    return is_high_surrogate && has_next ? SURROGATE_PAIR_LENGTH : 1;
}

}

namespace platform {

size_t rootLength(Span<const char> path)
{
    const char first = readLetter(path, 0);
    const char second = readLetter(path, 1);
    const char third = readLetter(path, DRIVE_LENGTH);
    const bool is_double_separator = isSeparator(first) && isSeparator(second);
    if (is_double_separator) { return DOUBLE_SEPARATOR_ROOT_LENGTH; }
    const bool has_drive = isLetter(first) && second == ':';
    const bool has_drive_root = has_drive && isSeparator(third);
    if (has_drive_root) { return DRIVE_ROOT_LENGTH; }
    if (has_drive) { return DRIVE_LENGTH; }
    return isSeparator(first) ? 1 : 0;
}

KiottySpecStatus convertPath(Span<const char> path, Span<NativeLetter> native)
{
    const bool is_valid = isValidUtf8(path);
    if (!is_valid) { return KIOTTY_SPEC_STATUS_INVALID_UTF8; }
    size_t index = 0;
    size_t written = 0;
    const size_t path_length = path.length();
    while (index < path_length) {
        const char lead = readLetter(path, index);
        const size_t length = utf8SequenceLength(static_cast<unsigned char>(lead));
        const uint32_t point = utf8CodePointAt(path, index, length);
        const WrittenResult wrote = writeCodePoint(native, written, point);
        if (!wrote) { return wrote.code; }
        written = wrote.value;
        index += length;
    }
    return writeTerminator(native, written);
}

DescriptorResult openFile(const NativePath& path, OpenMode mode)
{
    const int flags = mode == OpenMode::READ ? _O_RDONLY : (_O_WRONLY | _O_CREAT | _O_TRUNC);
    int descriptor = 0;
    const errno_t opened = _wsopen_s(&descriptor, path.text, flags | _O_BINARY, _SH_DENYNO,
                                     _S_IREAD | _S_IWRITE);
    if (opened != 0) { return DescriptorResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
    return DescriptorResult::success(descriptor);
}

int readSome(int descriptor, Span<char> buffer)
{ return _read(descriptor, buffer.data(), static_cast<unsigned int>(buffer.length())); }

int writeSome(int descriptor, Span<const char> content)
{
    return _write(descriptor, content.data(), static_cast<unsigned int>(content.length()));
}

void closeFile(int descriptor) { _close(descriptor); }

bool makeDirectory(const NativePath& path) { return _wmkdir(path.text) == 0 || errno == EEXIST; }

bool isDirectory(const NativePath& path)
{
    const ModeResult mode = readMode(path);
    return mode && (mode.value & _S_IFDIR) != 0;
}

bool isFile(const NativePath& path)
{
    const ModeResult mode = readMode(path);
    return mode && (mode.value & _S_IFREG) != 0;
}

KiottySpecStatus removeFile(const NativePath& path)
{
    return _wremove(path.text) == 0 ? KIOTTY_SPEC_STATUS_SUCCESS
                                    : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

KiottySpecStatus renameFile(const NativePath& from, const NativePath& to)
{
    const BOOL moved =
        MoveFileExW(from.text, to.text, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return moved != 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

bool isSeparator(char letter) { return letter == '/' || letter == '\\'; }

namespace {

ModeResult readMode(const NativePath& path)
{
    FileStatus status {};
    const int status_code = _wstat64(path.text, &status);
    if (status_code != 0) { return ModeResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return ModeResult::success(static_cast<uint32_t>(status.st_mode));
}

WrittenResult writeCodePoint(Span<wchar_t> native, size_t offset, uint32_t point)
{
    const uint32_t payload = point - UTF8_FOUR_BYTE_MIN_POINT;
    const wchar_t pair[SURROGATE_UNITS] = {
        static_cast<wchar_t>(HIGH_SURROGATE_BASE + (payload >> SURROGATE_PAYLOAD_BITS)),
        static_cast<wchar_t>(LOW_SURROGATE_BASE + (payload & SURROGATE_PAYLOAD_MASK))};
    const wchar_t single[1] = {static_cast<wchar_t>(point)};
    const bool is_pair = point >= UTF8_FOUR_BYTE_MIN_POINT;
    const Span<const wchar_t> encoded =
        is_pair ? Span<const wchar_t>(pair, SURROGATE_UNITS) : Span<const wchar_t>(single, 1);
    const size_t units = encoded.length();
    const Result<Span<wchar_t>, KiottySpecStatus> target = native.subspan(offset, units);
    if (!target) { return WrittenResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const KiottySpecStatus copied = target.value.copyFrom(encoded);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return WrittenResult::failure(copied); }
    return WrittenResult::success(offset + units);
}

KiottySpecStatus writeTerminator(Span<wchar_t> native, size_t length)
{
    const Result<wchar_t&, KiottySpecStatus> terminator = native.at(length);
    if (!terminator) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    *terminator.value = L'\0';
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

bool isLetter(char letter)
{ return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'); }

char readLetter(Span<const char> path, size_t index)
{
    const Result<const char&, KiottySpecStatus> letter = path.at(index);
    return letter ? *letter.value : '\0';
}

}

}

}

#endif
