#pragma once

#include "kiotty_spec.h"
#include "handle.h"
#include "result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Element>
class LazyPool
{
public:
    explicit LazyPool(size_t capacity)
        : _values(capacity), _generations(capacity, 0), _in_use(capacity, 0), _free_indices(capacity), _free_count(capacity)
    {
        for (size_t index = 0; index < capacity; ++index) {
            _free_indices[index] = static_cast<uint32_t>(capacity - 1 - index);
        }
    }

    Handle acquire()
    {
        if (_free_count == 0) { return Handle{Handle::INVALID_INDEX, 0}; }
        --_free_count;
        const uint32_t index = _free_indices[_free_count];
        _in_use[index] = 1;
        return Handle{index, _generations[index]};
    }

    Result<Element&, KiottySpecStatus> get(Handle handle)
    {
        if (!isLive(handle)) { return Result<Element&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE); }
        return Result<Element&, KiottySpecStatus>::success(_values[handle.index]);
    }

    KiottySpecStatus release(Handle handle)
    {
        if (!isLive(handle)) { return KIOTTY_SPEC_STATUS_INVALID_HANDLE; }
        _in_use[handle.index] = 0;
        ++_generations[handle.index];
        _free_indices[_free_count] = handle.index;
        ++_free_count;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    size_t capacity() const { return _values.size(); }

    // index 순서로 훑을 때 쓴다. 쓰고 있지 않은 index 면 무효 핸들이다.
    Handle handleAt(size_t index) const
    {
        if (index >= _values.size() || _in_use[index] == 0) { return Handle{Handle::INVALID_INDEX, 0}; }
        return Handle{static_cast<uint32_t>(index), _generations[index]};
    }

    // 모든 슬롯을 돌려준다. 쓰던 슬롯의 generation 이 올라가 이전 핸들은 모두 무효가 된다.
    // 큰 index 부터 돌려주어 다음 acquire 가 다시 작은 index 부터 나눠 준다.
    void releaseAll()
    {
        for (size_t index = _values.size(); index > 0; --index) {
            if (_in_use[index - 1] != 0) { release(Handle{static_cast<uint32_t>(index - 1), _generations[index - 1]}); }
        }
    }

private:
    bool isLive(Handle handle) const
    {
        if (handle.index >= _values.size()) { return false; }
        return _in_use[handle.index] != 0 && _generations[handle.index] == handle.generation;
    }

    std::vector<Element> _values;
    std::vector<uint32_t> _generations;
    std::vector<uint8_t> _in_use;
    std::vector<uint32_t> _free_indices;
    size_t _free_count;
};

}
