#pragma once

#include <cstdint>

namespace kiotty::spec::implementation {

struct Handle
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

    uint32_t index {INVALID_INDEX};
    uint16_t generation {};
    uint16_t type {};

    bool isValid() const { return index != INVALID_INDEX; }
};

static_assert(sizeof(Handle) == 8, "unexpected padding");

template <uint16_t Type>
class TypedHandle : public Handle
{
public:
    constexpr TypedHandle() : Handle {INVALID_INDEX, 0, Type} {}
    constexpr TypedHandle(uint32_t slot_index, uint16_t slot_generation)
        : Handle {slot_index, slot_generation, Type}
    {}
};

static_assert(sizeof(TypedHandle<0>) == sizeof(Handle), "unexpected padding");

}
