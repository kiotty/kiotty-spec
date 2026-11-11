#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 레코드 버전을 (정체성, checkpoint) 순으로 정렬해 둔 고정 용량 표 (개발수칙 1.6).
// Record 는 checkpoint · removed 필드와 Identity identity() const 를 가진다. Identity 는 operator< 와 == 를 가진다.
// "그 시점의 값" 은 정체성이 같고 checkpoint 이하인 마지막 버전이다.
template <typename Record>
class VersionTable
{
public:
    using Identity = typename Record::Identity;

    explicit VersionTable(size_t capacity) : _records(capacity), _length(0) {}

    void clear() { _length = 0; }

    // 적재할 때 쓴다. 다 넣은 뒤 sort 를 부른다.
    KiottySpecStatus append(const Record& record)
    {
        if (_length == _records.size()) { return KIOTTY_SPEC_STATUS_POOL_IS_FULL; }
        _records[_length] = record;
        ++_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void sort() { std::sort(_records.begin(), _records.begin() + static_cast<std::ptrdiff_t>(_length), less); }

    // 정렬 뒤 같은 정체성 · 같은 checkpoint 가 두 번 나오면 둘째의 index. 병합 충돌이다.
    Result<size_t, KiottySpecStatus> findDuplicate() const
    {
        for (size_t index = 1; index < _length; ++index) {
            const bool same = _records[index - 1].identity() == _records[index].identity();
            if (same && _records[index - 1].checkpoint == _records[index].checkpoint) { return Result<size_t, KiottySpecStatus>::success(index); }
        }
        return Result<size_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
    }

    // (identity, checkpoint) 버전이 있으면 그 자리를, 없으면 정렬 위치에 빈 레코드를 넣어 돌려준다.
    // 호출자는 정체성 필드를 identity 와 같게 채운다.
    Result<Record&, KiottySpecStatus> slot(Identity identity, uint32_t checkpoint)
    {
        const size_t position = lowerBound(identity, checkpoint);
        const bool found = position < _length && _records[position].identity() == identity && _records[position].checkpoint == checkpoint;
        if (found) { return Result<Record&, KiottySpecStatus>::success(_records[position]); }
        if (_length == _records.size()) { return Result<Record&, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_POOL_IS_FULL); }
        for (size_t index = _length; index > position; --index) { _records[index] = _records[index - 1]; }
        _records[position] = emptyRecord();
        _records[position].checkpoint = checkpoint;
        ++_length;
        return Result<Record&, KiottySpecStatus>::success(_records[position]);
    }

    KiottySpecStatus write(const Record& record)
    {
        const auto target = slot(record.identity(), record.checkpoint);
        if (!target) { return target.code; }
        *target.value = record;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    // 정체성이 identity 이고 checkpoint 이하인 마지막 버전. 없으면 nullptr
    const Record* latest(Identity identity, uint32_t checkpoint) const
    {
        const size_t after = lowerBound(identity, static_cast<uint64_t>(checkpoint) + 1);
        if (after == 0 || !(_records[after - 1].identity() == identity)) { return nullptr; }
        return &_records[after - 1];
    }

    Span<const Record> all() const { return Span<const Record>(_records.data(), _length); }

    size_t countAt(uint32_t checkpoint) const
    {
        size_t count = 0;
        for (size_t index = 0; index < _length; ++index) { count += _records[index].checkpoint == checkpoint ? 1 : 0; }
        return count;
    }

    // 정리 (설계서 v1 5.3). base 이하의 버전은 정체성마다 마지막 하나만 base 로 올려 남기고, 그것이 지움이면 그것도 버린다.
    void prune(uint32_t base)
    {
        size_t kept = 0;
        size_t first = 0;
        while (first < _length) {
            const size_t end = groupEnd(first);
            kept = pruneGroup(first, end, base, kept);
            first = end;
        }
        _length = kept;
    }

    // keep 이 참인 버전만 남긴다. 순서는 그대로다.
    template <typename Keep>
    void retain(Keep keep)
    {
        size_t kept = 0;
        for (size_t index = 0; index < _length; ++index) {
            if (!keep(_records[index])) { continue; }
            if (kept != index) { _records[kept] = _records[index]; }
            ++kept;
        }
        _length = kept;
    }

private:
    static bool less(const Record& left, const Record& right)
    {
        if (left.identity() < right.identity()) { return true; }
        if (right.identity() < left.identity()) { return false; }
        return left.checkpoint < right.checkpoint;
    }

    static const Record& emptyRecord()
    {
        static const Record EMPTY {};
        return EMPTY;
    }

    size_t lowerBound(Identity identity, uint64_t checkpoint) const
    {
        size_t low = 0;
        size_t high = _length;
        while (low < high) {
            const size_t middle = low + (high - low) / 2;
            const Record& record = _records[middle];
            const bool before = record.identity() < identity || (record.identity() == identity && record.checkpoint < checkpoint);
            if (before) { low = middle + 1; } else { high = middle; }
        }
        return low;
    }

    size_t groupEnd(size_t first) const
    {
        size_t end = first + 1;
        while (end < _length && _records[end].identity() == _records[first].identity()) { ++end; }
        return end;
    }

    size_t pruneGroup(size_t first, size_t end, uint32_t base, size_t kept)
    {
        size_t last_old = end;
        for (size_t index = first; index < end && _records[index].checkpoint <= base; ++index) { last_old = index; }
        for (size_t index = first; index < end; ++index) {
            const bool older = _records[index].checkpoint <= base;
            const bool survivor = index == last_old && _records[index].removed == 0;
            if (older && !survivor) { continue; }
            if (kept != index) { _records[kept] = _records[index]; }
            if (older) { _records[kept].checkpoint = base; }
            ++kept;
        }
        return kept;
    }

    std::vector<Record> _records;
    size_t _length;
};

}
