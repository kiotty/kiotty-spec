#pragma once

#include <cstddef>

namespace kiotty::spec::implementation {

static_assert(sizeof(void*) == 8 || sizeof(void*) == 4, "unsupported pointer size");

// static_assert(sizeof) 로 고정하는 크기. 포인터 · size_t 가 든 struct 는 64 비트 빌드와
// 32 비트 빌드 (WebAssembly) 의 크기가 다르므로 둘을 함께 적는다.
constexpr size_t layoutSize(size_t bytes_on_64bit, size_t bytes_on_32bit)
{
    if (sizeof(void*) == 8) { return bytes_on_64bit; }
    return bytes_on_32bit;
}

}
