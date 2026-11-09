#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace kiotty::spec::implementation {

// 정렬 배열 + 이진 탐색 (개발수칙 1.6). 용량은 생성할 때 한 번 확보하고 늘리지 않는다.
// Key 는 operator< 로 순서를 정한다.
template <typename Key, typename Value>
class FixedMap
{
public:
    explicit FixedMap(size_t capacity) : _keys(capacity), _values(capacity), _length(0) {}

    KiottySpecStatus insert(const Key& key, Value value)
    {
        const size_t position = lowerBound(key);
        if (position < _length && !(key < _keys[position])) { return KIOTTY_SPEC_STATUS_CONFLICT; }
        if (_length == _keys.size()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = _length; index > position; --index) {
            _keys[index] = _keys[index - 1];
            _values[index] = std::move(_values[index - 1]);
        }
        _keys[position] = key;
        _values[position] = std::move(value);
        ++_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Result<Value, KiottySpecStatus> find(const Key& key) const
    {
        const size_t position = lowerBound(key);
        if (position == _length || key < _keys[position]) {
            return Result<Value, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
        }
        return Result<Value, KiottySpecStatus>::success(_values[position]);
    }

    KiottySpecStatus erase(const Key& key)
    {
        const size_t position = lowerBound(key);
        if (position == _length || key < _keys[position]) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
        for (size_t index = position + 1; index < _length; ++index) {
            _keys[index - 1] = _keys[index];
            _values[index - 1] = std::move(_values[index]);
        }
        --_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear() { _length = 0; }

    size_t length() const { return _length; }
    size_t capacity() const { return _keys.size(); }
    const Key& keyAt(size_t index) const { return _keys[index]; }
    const Value& valueAt(size_t index) const { return _values[index]; }

private:
    size_t lowerBound(const Key& key) const
    {
        size_t low = 0;
        size_t high = _length;
        while (low < high) {
            const size_t middle = low + (high - low) / 2;
            if (_keys[middle] < key) { low = middle + 1; } else { high = middle; }
        }
        return low;
    }

    std::vector<Key> _keys;
    std::vector<Value> _values;
    size_t _length;
};

}
