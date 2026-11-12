#include "implementation/data/datasource/kiotty_kisp_file.h"

#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

uint64_t hashLine(Span<const char> line, uint64_t hash);

}

Result<uint64_t, KiottySpecStatus> hashKispFile(const FileSystem& file_system,
                                                Span<const char> path, Span<char> buffer)
{
    OpenedFileResult opened = openKispFile(file_system, path);
    if (opened.code == KIOTTY_SPEC_STATUS_NOT_FOUND) {
        return Result<uint64_t, KiottySpecStatus>::success(EMPTY_KISP_HASH);
    }
    if (!opened) { return Result<uint64_t, KiottySpecStatus>::failure(opened.code); }
    KispFileReader reader(std::move(opened.value), buffer, Span<char>());
    Result<bool, KiottySpecStatus> next = reader.next();
    while (next && next.value) { next = reader.next(); }
    if (!next) { return Result<uint64_t, KiottySpecStatus>::failure(next.code); }
    return Result<uint64_t, KiottySpecStatus>::success(reader.hash());
}

OpenedFileResult openKispFile(const FileSystem& file_system, Span<const char> path)
{
    const bool is_present = file_system.isFile(path);
    if (!is_present) { return OpenedFileResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    OpenedFileResult opened = OpenedFile::openForReading(file_system, path);
    if (!opened) { opened.code = KIOTTY_SPEC_STATUS_INPUT_OUTPUT; }
    return opened;
}

Result<bool, KiottySpecStatus> KispFileReader::next()
{
    using LineResult = Result<bool, KiottySpecStatus>;
    const LineResult read = _file.next();
    const bool has_line = read && read.value;
    if (has_line) {
        ++_line_number;
        _hash = hashLine(_file.line(), _hash);
    }
    return read;
}

KispFileWriter::~KispFileWriter()
{
    const bool is_abandoned = _file.isOpen();
    if (is_abandoned) {
        _file.close();
        _file_system.removeFile(_temporary_path);
    }
}

KiottySpecStatus KispFileWriter::write(Span<const char> line)
{
    _hash = hash64(line, _hash);
    return _file.write(line);
}

KiottySpecStatus KispFileWriter::commit()
{
    _file.close();
    const KiottySpecStatus renamed = _file_system.renameFile(_temporary_path, _path);
    if (renamed != KIOTTY_SPEC_STATUS_SUCCESS) { _file_system.removeFile(_temporary_path); }
    return renamed;
}

namespace {

uint64_t hashLine(Span<const char> line, uint64_t hash)
{
    const uint64_t content = hash64(line, hash);
    return hash64(literalText("\n"), content);
}

}

}
