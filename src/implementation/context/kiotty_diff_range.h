#pragma once

#include <cstdint>

namespace kiotty::spec::implementation {

// 비교할 두 체크포인트 sequence
struct DiffRange {
    uint32_t from {0};
    uint32_t to {0};
};

}
