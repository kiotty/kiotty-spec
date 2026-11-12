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

    Result<Span<Element>, KiottySpecStatus> subspan(size_t offset, size_t length) const
    {
        using SubspanResult = Result<Span<Element>, KiottySpecStatus>;
        const bool is_outside = isOutsideRange(offset, length, _length);
        if (is_outside) { return SubspanResult::failure(KIOTTY_SPEC_STATUS_OUT_OF_RANGE); }
        const Span<Element> window(advance(_data, offset), length);
        return SubspanResult::success(window);
    }

    KiottySpecStatus copyFrom(Span<const Element> source) const
    { return copyElements(_data, _length, source); }

private:
    static KiottySpecStatus copyElements(Element* data, size_t capacity,
                                         Span<const Element> source)
    {
        const size_t source_length = source.length();
        if (source_length > capacity) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < source_length; ++index) { data[index] = source[index]; }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    static Element* advance(Element* data, size_t length) { return data + length; }
    static bool isOutside(size_t index, size_t length) { return index >= length; }
    static bool isOutsideRange(size_t offset, size_t length, size_t total)
    { return offset > total || length > total - offset; }

    Element* _data;
    size_t _length;
};

template <typename Element>
Span<Element> viewVector(std::vector<Element>& values)
{ return Span<Element>(values.data(), values.size()); }

template <typename Element>
Span<const Element> viewVector(const std::vector<Element>& values)
{ return Span<const Element>(values.data(), values.size()); }

}
