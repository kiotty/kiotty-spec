#include "implementation/data/datasource/kiotty_file_reader.h"

#include "implementation/data/datasource/kiotty_file_system.h"

namespace kiotty::spec::implementation {

FileReader::~FileReader()
{
    if (_descriptor >= 0) { closeDescriptor(_descriptor); }
}

KiottySpecStatus FileReader::open(Span<const char> path)
{
    if (_descriptor >= 0) { closeDescriptor(_descriptor); }
    _descriptor = openForReading(path);
    _start = 0;
    _end = 0;
    _finished = false;
    return _descriptor >= 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_NOT_FOUND;
}

Result<bool, KiottySpecStatus> FileReader::next()
{
    using LineResult = Result<bool, KiottySpecStatus>;
    if (_descriptor < 0) { return LineResult::failure(KIOTTY_SPEC_STATUS_PRECONDITION); }
    size_t scan = _start;
    while (true) {
        while (scan < _end && _buffer[scan] != '\n') { ++scan; }
        if (scan < _end || (_finished && _start < _end)) { break; }
        if (_finished) { return LineResult::success(false); }
        // 남은 조각을 앞으로 당기고 더 읽는다.
        const size_t kept = _end - _start;
        for (size_t index = 0; index < kept; ++index) { _buffer[index] = _buffer[_start + index]; }
        _start = 0;
        _end = kept;
        scan = kept;
        if (_end == _buffer.length()) { return LineResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
        const int count = readDescriptor(_descriptor, Span<char>(_buffer.data() + _end, _buffer.length() - _end));
        if (count < 0) { return LineResult::failure(KIOTTY_SPEC_STATUS_INPUT_OUTPUT); }
        if (count == 0) { _finished = true; }
        _end += static_cast<size_t>(count);
    }
    size_t last = scan;
    while (last > _start && (_buffer[last - 1] == '\r' || _buffer[last - 1] == ' ')) { --last; }
    _line = Span<const char>(_buffer.data() + _start, last - _start);
    _start = scan < _end ? scan + 1 : _end;
    return LineResult::success(true);
}

}
