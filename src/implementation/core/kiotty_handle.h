#pragma once

#include <cstdint>

namespace kiotty::spec::implementation {

struct Handle
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

    uint32_t index {};
    uint32_t generation {};

    bool isValid() const { return index != INVALID_INDEX; }
};

static_assert(sizeof(Handle) == 8, "unexpected padding");

}
