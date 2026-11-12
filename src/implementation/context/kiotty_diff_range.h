#pragma once

#include <cstdint>

namespace kiotty::spec::implementation {

struct DiffRange {
    uint32_t from {0};
    uint32_t to {0};
};

static_assert(sizeof(DiffRange) == 8, "unexpected padding");

}
