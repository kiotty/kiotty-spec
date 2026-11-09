#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

struct TagPair {
    uint32_t tag {0};
    uint64_t value {0};
};

// 버전 행의 태그 글자 "[[1,4],[2,1]]" 을 (tag, value) 쌍으로 차례로 읽는다 (설계서 v1 3.6).
class TagPairs
{
public:
    explicit TagPairs(Span<const char> text) : _text(text) {}

    // 다음 쌍이 있으면 true 이고 current() 가 그 쌍이다. 형식이 어긋나면 INVALID_ARGUMENT
    Result<bool, KiottySpecStatus> next();
    TagPair current() const { return _current; }

private:
    Span<const char> _text;
    size_t _position {0};
    uint64_t _numbers[2] {};
    size_t _count {0};
    uint64_t _number {0};
    bool _in_number {false};
    TagPair _current {};
};

}
