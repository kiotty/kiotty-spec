#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace kiotty::spec::implementation {

template <typename Element, size_t Capacity>
class FixedVector
{
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert(std::is_default_constructible<Element>::value,
                  "Element must be default constructible");

public:
    KiottySpecStatus append(Element element)
    {
        const bool is_full = isFull(_length);
        if (is_full) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        _values[_length] = std::move(element);
        ++_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear() { _length = 0; }

    size_t length() const { return _length; }
    static constexpr size_t capacity() { return Capacity; }

    Element& operator[](size_t index) { return _values[index]; }
    const Element& operator[](size_t index) const { return _values[index]; }

    Result<Element&, KiottySpecStatus> at(size_t index)
    {
        const bool is_outside = isOutside(index, _length);
        if (is_outside) {
            return Result<Element&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_OUT_OF_RANGE);
        }
        return Result<Element&, KiottySpecStatus>::success(_values[index]);
    }

private:
    static bool isFull(size_t length) { return length == Capacity; }
    static bool isOutside(size_t index, size_t length) { return index >= length; }

    Element _values[Capacity] {};
    size_t _length {0};
};

}
