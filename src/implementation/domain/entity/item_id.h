#pragma once

#include "kiotty_spec.h"
#include "implementation/core/span.h"

namespace kiotty::spec::implementation::entity {

// 설계서 v1 3.2. 글자는 [A-Za-z0-9_-], 첫 글자는 [A-Za-z0-9], 1 ~ 15 바이트.
// 빈 ID 와 허용하지 않는 글자는 INVALID_ARGUMENT, 15 바이트를 넘으면 CAPACITY_EXCEEDED.
KiottySpecStatus validateItemId(Span<const char> id);

}
