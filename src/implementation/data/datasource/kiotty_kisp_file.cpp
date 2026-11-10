#include "implementation/data/datasource/kiotty_kisp_file.h"

#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char TEMPORARY_SUFFIX[] = ".tmp";

uint64_t hashLine(Span<const char> line, uint64_t hash)
{
    const uint64_t content = hash64(line, hash);
    return hash64(literalText("\n"), content);
}

}

KiottySpecStatus KispFileReader::open(Span<const char> path)
{
    _line_number = 0;
    _hash = EMPTY_KISP_HASH;
    _present = isFile(path);
    if (!_present) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    return _file.open(path) == KIOTTY_SPEC_STATUS_SUCCESS ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
}

Result<bool, KiottySpecStatus> KispFileReader::next()
{
    if (!_present) { return Result<bool, KiottySpecStatus>::success(false); }
    const Result<bool, KiottySpecStatus> read = _file.next();
    if (!read || !read.value) { return read; }
    ++_line_number;
    _hash = hashLine(_file.line(), _hash);
    return read;
}

KispFileWriter::~KispFileWriter()
{
    if (!_open) { return; }
    _file.close();
    removeFile(_temporary.view());
}

KiottySpecStatus KispFileWriter::open(Span<const char> path)
{
    _hash = EMPTY_KISP_HASH;
    KiottySpecStatus status = _path.assign(path);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _temporary.assign(path); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _temporary.append(literalText(TEMPORARY_SUFFIX)); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = _file.open(_temporary.view());
    _open = status == KIOTTY_SPEC_STATUS_SUCCESS;
    return status;
}

KiottySpecStatus KispFileWriter::write(Span<const char> line)
{
    _hash = hash64(line, _hash);
    return _file.write(line);
}

KiottySpecStatus KispFileWriter::commit()
{
    _file.close();
    _open = false;
    const KiottySpecStatus renamed = renameFile(_temporary.view(), _path.view());
    if (renamed != KIOTTY_SPEC_STATUS_SUCCESS) { removeFile(_temporary.view()); }
    return renamed;
}

Result<uint64_t, KiottySpecStatus> hashKispFile(Span<const char> path, Span<char> buffer)
{
    KispFileReader reader(buffer, Span<char>());
    const KiottySpecStatus opened = reader.open(path);
    if (opened == KIOTTY_SPEC_STATUS_NOT_FOUND) { return Result<uint64_t, KiottySpecStatus>::success(EMPTY_KISP_HASH); }
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<uint64_t, KiottySpecStatus>::failure(opened); }
    Result<bool, KiottySpecStatus> next = reader.next();
    while (next && next.value) { next = reader.next(); }
    if (!next) { return Result<uint64_t, KiottySpecStatus>::failure(next.code); }
    return Result<uint64_t, KiottySpecStatus>::success(reader.hash());
}

}
