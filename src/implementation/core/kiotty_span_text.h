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
        if (_area.length() > 0) { _area[0] = '\0'; }
    }

    KiottySpecStatus append(Span<const char> text)
    {
        if (_area.length() == 0 || text.length() > capacity() - _length) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < text.length(); ++index) { _area[_length + index] = text[index]; }
        _length += text.length();
        _area[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> view() const { return Span<const char>(_area.data(), _length); }
    size_t capacity() const { return _area.length() == 0 ? 0 : _area.length() - 1; }

private:
    Span<char> _area;
    size_t _length {0};
};

}
