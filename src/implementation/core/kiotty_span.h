#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"

#include <cstddef>
#include <type_traits>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Element>
class Span
{
public:
    Span() : _data(nullptr), _length(0) {}
    Span(Element* data, size_t length) : _data(data), _length(length) {}

    template <typename Other,
              typename = std::enable_if_t<std::is_same<const Other, Element>::value>>
    Span(Span<Other> other) : _data(other.data()), _length(other.length())
    {}

    Element* data() const { return _data; }
    size_t length() const { return _length; }
    Element* begin() const { return _data; }
    Element* end() const { return advance(_data, _length); }

    Element& operator[](size_t index) const { return _data[index]; }

    Result<Element&, KiottySpecStatus> at(size_t index) const
    {
        const bool is_outside = isOutside(index, _length);
        if (is_outside) {
            return Result<Element&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_OUT_OF_RANGE);
        }
        return Result<Element&, KiottySpecStatus>::success(_data[index]);
    }

private:
    static Element* advance(Element* data, size_t length) { return data + length; }
    static bool isOutside(size_t index, size_t length) { return index >= length; }

    Element* _data;
    size_t _length;
};

template <typename Element>
Span<Element> viewVector(std::vector<Element>& values)
{ return Span<Element>(values.data(), values.size()); }

}
