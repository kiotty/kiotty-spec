#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"

#include <cstddef>
#include <type_traits>

namespace kiotty::spec::implementation {

template <typename Element>
class Span
{
public:
    Span() : _data(nullptr), _length(0) {}
    Span(Element* data, size_t length) : _data(data), _length(length) {}

    template <typename Other, typename = std::enable_if_t<std::is_same<const Other, Element>::value>>
    Span(Span<Other> other) : _data(other.data()), _length(other.length()) {}

    Element* data() const { return _data; }
    size_t length() const { return _length; }
    Element* begin() const { return _data; }
    Element* end() const { return _data + _length; }

    Element& operator[](size_t index) const { return _data[index]; }

    Result<Element&, KiottySpecStatus> at(size_t index) const
    {
        if (index >= _length) { return Result<Element&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
        return Result<Element&, KiottySpecStatus>::success(_data[index]);
    }

    KiottySpecStatus copyFrom(Span<const Element> source) const
    {
        if (source.length() > _length) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < source.length(); ++index) { _data[index] = source[index]; }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

private:
    Element* _data;
    size_t _length;
};

}
