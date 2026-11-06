#pragma once

#include "kiotty_spec.h"
#include "span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

template <size_t Capacity>
class FixedString
{
public:
    KiottySpecStatus assign(Span<const char> text)
    {
        clear();
        return append(text);
    }

    KiottySpecStatus append(Span<const char> text)
    {
        if (text.length() > Capacity - _length) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < text.length(); ++index) { _data[_length + index] = text[index]; }
        _length += text.length();
        _data[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear()
    {
        _length = 0;
        _data[0] = '\0';
    }

    size_t length() const { return _length; }
    static constexpr size_t capacity() { return Capacity; }
    const char* c_str() const { return _data; }
    Span<const char> view() const { return Span<const char>(_data, _length); }

private:
    char _data[Capacity + 1] {};
    size_t _length {0};
};

}
