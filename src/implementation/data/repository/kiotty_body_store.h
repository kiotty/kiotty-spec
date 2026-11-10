#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 본문이 놓인 자리
struct BodySlice {
    size_t offset;
    size_t length;
};

// 항목 key 마다 현재 본문을 둔다. 바이트 영역과 자리 표는 초기화 때 한 번 확보한다 (개발수칙 1.1).
// 고친 본문은 영역 끝에 붙이고, 끝이 모자라면 살아 있는 본문을 앞으로 당겨 빈자리를 없앤다.
class BodyStore
{
public:
    BodyStore(size_t bytes, size_t keys) : _bytes(bytes), _slices(keys), _order(keys) {}

    // 빈 본문은 지운다. 영역이 모자라면 CAPACITY_EXCEEDED, 자리 표가 모자라면 POOL_IS_FULL
    KiottySpecStatus put(uint32_t key, Span<const char> body);
    // 없으면 빈 본문
    Span<const char> find(uint32_t key) const;
    void erase(uint32_t key);
    void clear();
    // key 순서로 훑을 때 쓴다.
    size_t length() const { return _slices.length(); }
    uint32_t keyAt(size_t index) const { return _slices.keyAt(index); }
    Span<const char> bodyAt(size_t index) const;

private:
    void compact();

    std::vector<char> _bytes;
    FixedMap<uint32_t, BodySlice> _slices;
    std::vector<uint32_t> _order;
    size_t _used {0};
};

// 본문을 buffer 에 null 종료로 복사한다. 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수
Result<size_t, KiottySpecStatus> copyBody(Span<const char> body, char* buffer, size_t capacity);

}
