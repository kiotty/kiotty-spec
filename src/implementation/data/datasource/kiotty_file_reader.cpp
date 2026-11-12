#include "implementation/data/datasource/kiotty_file_reader.h"

#include "implementation/data/datasource/kiotty_file_system.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

struct LineWindow {
    Span<const char> line {};
    size_t start {0};
    size_t end {0};
    size_t scan {0};
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
    bool has_line {false};
    bool is_finished {false};
};

static_assert(sizeof(LineWindow) == 48, "unexpected padding");

void closeOpenDescriptor(int descriptor);
KiottySpecStatus checkOpened(int descriptor);
bool isClosedDescriptor(int descriptor);
LineWindow readLine(int descriptor, Span<char> buffer, const LineWindow& initial);
LineWindow readMore(int descriptor, Span<char> buffer, const LineWindow& window);
LineWindow copyToFront(Span<char> buffer, const LineWindow& window);
LineWindow cutLine(Span<const char> buffer, const LineWindow& window);
size_t findNewline(Span<const char> buffer, size_t from, size_t end);
bool isCarriageReturnAt(Span<const char> buffer, size_t index);

}

FileReader::~FileReader() { closeOpenDescriptor(_descriptor); }

KiottySpecStatus FileReader::open(Span<const char> path)
{
    closeOpenDescriptor(_descriptor);
    const DescriptorResult opened = openForReading(path);
    _descriptor = opened ? opened.value : CLOSED_DESCRIPTOR;
    _start = 0;
    _end = 0;
    _is_finished = false;
    return checkOpened(_descriptor);
}

Result<bool, KiottySpecStatus> FileReader::next()
{
    using LineResult = Result<bool, KiottySpecStatus>;
    const bool is_closed = isClosedDescriptor(_descriptor);
    if (is_closed) { return LineResult::failure(KIOTTY_SPEC_STATUS_PRECONDITION); }
    LineWindow window {};
    window.start = _start;
    window.end = _end;
    window.is_finished = _is_finished;
    window = readLine(_descriptor, _buffer, window);
    _start = window.start;
    _end = window.end;
    _is_finished = window.is_finished;
    if (window.has_line) { _line = window.line; }
    if (window.status != KIOTTY_SPEC_STATUS_SUCCESS) { return LineResult::failure(window.status); }
    return LineResult::success(window.has_line);
}

namespace {

void closeOpenDescriptor(int descriptor)
{
    if (descriptor >= 0) { closeDescriptor(descriptor); }
}

KiottySpecStatus checkOpened(int descriptor)
{ return descriptor >= 0 ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_NOT_FOUND; }

bool isClosedDescriptor(int descriptor) { return descriptor < 0; }

LineWindow readLine(int descriptor, Span<char> buffer, const LineWindow& initial)
{
    LineWindow window = initial;
    window.scan = findNewline(buffer, window.start, window.end);
    while (window.status == KIOTTY_SPEC_STATUS_SUCCESS) {
        const bool has_newline = window.scan < window.end;
        const bool has_tail = window.is_finished && window.start < window.end;
        if (has_newline || has_tail) { return cutLine(buffer, window); }
        if (window.is_finished) { return window; }
        window = readMore(descriptor, buffer, window);
        window.scan = findNewline(buffer, window.scan, window.end);
    }
    return window;
}

LineWindow readMore(int descriptor, Span<char> buffer, const LineWindow& window)
{
    LineWindow moved = copyToFront(buffer, window);
    const size_t capacity = buffer.length();
    const bool is_full = moved.end == capacity;
    if (is_full) { moved.status = KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    if (moved.status != KIOTTY_SPEC_STATUS_SUCCESS) { return moved; }
    const Result<Span<char>, KiottySpecStatus> free_space =
        buffer.subspan(moved.end, capacity - moved.end);
    const int count = readDescriptor(descriptor, free_space.value);
    if (count < 0) {
        moved.status = KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
        return moved;
    }
    moved.is_finished = count == 0;
    moved.end += static_cast<size_t>(count);
    return moved;
}

LineWindow copyToFront(Span<char> buffer, const LineWindow& window)
{
    LineWindow moved = window;
    const size_t kept = window.end - window.start;
    for (size_t index = 0; index < kept; ++index) {
        const Result<char&, KiottySpecStatus> source = buffer.at(window.start + index);
        const Result<char&, KiottySpecStatus> target = buffer.at(index);
        const bool is_in_range = source && target;
        if (is_in_range) {
            *target.value = *source.value;
            continue;
        }
        moved.status = KIOTTY_SPEC_STATUS_OUT_OF_RANGE;
        return moved;
    }
    moved.start = 0;
    moved.end = kept;
    moved.scan = kept;
    return moved;
}

LineWindow cutLine(Span<const char> buffer, const LineWindow& window)
{
    size_t last = window.scan;
    const bool has_carriage_return = last > window.start && isCarriageReturnAt(buffer, last - 1);
    if (has_carriage_return) { --last; }
    LineWindow cut = window;
    const Result<Span<const char>, KiottySpecStatus> line =
        buffer.subspan(window.start, last - window.start);
    cut.line = line.value;
    cut.start = window.scan < window.end ? window.scan + 1 : window.end;
    cut.has_line = true;
    return cut;
}

size_t findNewline(Span<const char> buffer, size_t from, size_t end)
{
    const size_t limit = std::min(end, buffer.length());
    for (size_t index = from; index < limit; ++index) {
        const bool is_newline = buffer[index] == '\n';
        if (is_newline) { return index; }
    }
    return end;
}

bool isCarriageReturnAt(Span<const char> buffer, size_t index)
{
    const Result<const char&, KiottySpecStatus> letter = buffer.at(index);
    return letter && *letter.value == '\r';
}

}

}
