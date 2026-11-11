#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

class SpanText
{
public:
    explicit SpanText(Span<char> area) : _area(area)
    {
        const size_t area_length = _area.length();
        if (area_length > 0) { _area[0] = '\0'; }
    }

    KiottySpecStatus append(Span<const char> text)
    {
        const size_t area_length = _area.length();
        const size_t text_length = text.length();
        const size_t available = capacity() - _length;
        if (area_length == 0 || text_length > available) {
            return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
        }
        writeAt(_area, _length, text);
        _length += text_length;
        _area[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> view() const { return Span<const char>(_area.data(), _length); }
    size_t capacity() const { return measureCapacity(_area.length()); }

private:
    static void writeAt(Span<char> area, size_t offset, Span<const char> text)
    {
        const size_t text_length = text.length();
        for (size_t index = 0; index < text_length; ++index) { area[offset + index] = text[index]; }
    }

    static size_t measureCapacity(size_t area_length)
    { return area_length == 0 ? 0 : area_length - 1; }

    Span<char> _area;
    size_t _length {0};
};

}
