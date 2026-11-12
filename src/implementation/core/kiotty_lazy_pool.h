#pragma once

#include "kiotty_spec.h"
#include "kiotty_handle.h"
#include "kiotty_result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

template <typename Element, typename HandleValue = Handle>
class LazyPool
{
public:
    explicit LazyPool(size_t capacity)
        : _values(capacity), _generations(capacity, 0), _in_use(capacity, 0),
          _free_indices(capacity), _free_count(capacity)
    {
        for (size_t index = 0; index < capacity; ++index) {
            _free_indices[index] = static_cast<uint32_t>(capacity - 1 - index);
        }
    }

    HandleValue acquire()
    {
        const bool is_exhausted = isExhausted(_free_count);
        if (is_exhausted) { return HandleValue {}; }
        --_free_count;
        const uint32_t index = _free_indices[_free_count];
        _in_use[index] = 1;
        return HandleValue {index, _generations[index]};
    }

    Result<Element&, KiottySpecStatus> get(Handle handle)
    {
        const bool is_live = isLive(_in_use, _generations, _values.size(), handle);
        if (!is_live) {
            return Result<Element&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_HANDLE);
        }
        return Result<Element&, KiottySpecStatus>::success(_values[handle.index]);
    }

    Result<const Element&, KiottySpecStatus> find(Handle handle) const
    {
        const bool is_live = isLive(_in_use, _generations, _values.size(), handle);
        if (!is_live) {
            return Result<const Element&, KiottySpecStatus>::failure(
                KIOTTY_SPEC_STATUS_INVALID_HANDLE);
        }
        return Result<const Element&, KiottySpecStatus>::success(_values[handle.index]);
    }

    void releaseAll()
    {
        for (size_t index = _values.size(); index > 0; --index) {
            const bool is_in_use = isInUse(_in_use, index - 1);
            if (is_in_use) {
                release(HandleValue {static_cast<uint32_t>(index - 1), _generations[index - 1]});
            }
        }
    }

    KiottySpecStatus release(Handle handle)
    {
        const bool is_live = isLive(_in_use, _generations, _values.size(), handle);
        if (!is_live) { return KIOTTY_SPEC_STATUS_INVALID_HANDLE; }
        _in_use[handle.index] = 0;
        ++_generations[handle.index];
        _free_indices[_free_count] = handle.index;
        ++_free_count;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    size_t capacity() const { return _values.size(); }

    HandleValue handleAt(size_t index) const
    {
        const bool is_vacant = isVacant(_in_use, _values.size(), index);
        if (is_vacant) { return HandleValue {}; }
        return HandleValue {static_cast<uint32_t>(index), _generations[index]};
    }

private:
    static bool isExhausted(size_t free_count) { return free_count == 0; }

    static bool isInUse(const std::vector<uint8_t>& in_use, size_t index)
    { return in_use[index] != 0; }

    static bool isVacant(const std::vector<uint8_t>& in_use, size_t value_count, size_t index)
    { return index >= value_count || in_use[index] == 0; }

    static bool isLive(const std::vector<uint8_t>& in_use, const std::vector<uint16_t>& generations,
                       size_t value_count, Handle handle)
    {
        const bool is_other_type = handle.type != HandleValue {}.type;
        if (is_other_type || handle.index >= value_count) { return false; }
        return in_use[handle.index] != 0 && generations[handle.index] == handle.generation;
    }

    std::vector<Element> _values;
    std::vector<uint16_t> _generations;
    std::vector<uint8_t> _in_use;
    std::vector<uint32_t> _free_indices;
    size_t _free_count;
};

}
