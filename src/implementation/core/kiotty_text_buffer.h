#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <cstddef>
#include <vector>

namespace kiotty::spec::implementation {

class TextBuffer
{
public:
    explicit TextBuffer(size_t capacity) : _data(capacity + 1, '\0'), _length(0) {}

    Span<char> scratch()
    {
        clear();
        return Span<char>(_data.data(), capacity());
    }

    void clear()
    {
        _length = 0;
        _data[0] = '\0';
    }

    KiottySpecStatus append(Span<const char> text)
    {
        const size_t text_length = text.length();
        const size_t available = capacity() - _length;
        if (text_length > available) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        writeAt(viewVector(_data), _length, text);
        _length += text_length;
        _data[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> view() const { return Span<const char>(_data.data(), _length); }
    size_t capacity() const { return measureCapacity(_data.size()); }

private:
    static void writeAt(Span<char> area, size_t offset, Span<const char> text)
    {
        const size_t text_length = text.length();
        for (size_t index = 0; index < text_length; ++index) { area[offset + index] = text[index]; }
    }

    static size_t measureCapacity(size_t data_length) { return data_length - 1; }

    std::vector<char> _data;
    size_t _length;
};

}
