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
