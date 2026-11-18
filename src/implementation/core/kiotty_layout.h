#pragma once

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t POINTER_BYTES_64BIT = 8;
constexpr size_t POINTER_BYTES_32BIT = 4;

constexpr bool IS_KNOWN_POINTER_SIZE =
    sizeof(void*) == POINTER_BYTES_64BIT || sizeof(void*) == POINTER_BYTES_32BIT;
static_assert(IS_KNOWN_POINTER_SIZE, "unsupported pointer size");

constexpr size_t selectLayoutSize(size_t bytes_on_64bit, size_t bytes_on_32bit)
{
    if (sizeof(void*) == POINTER_BYTES_64BIT) { return bytes_on_64bit; }
    return bytes_on_32bit;
}

}
