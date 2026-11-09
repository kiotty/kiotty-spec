#include "implementation/domain/entity/kiotty_tag_pairs.h"

namespace kiotty::spec::implementation::entity {

namespace {

constexpr uint64_t DECIMAL_BASE = 10;
constexpr size_t PAIR_SIZE = 2;

}

Result<bool, KiottySpecStatus> TagPairs::next()
{
    using PairResult = Result<bool, KiottySpecStatus>;
    while (_position < _text.length()) {
        const char letter = _text[_position++];
        if (letter >= '0' && letter <= '9') {
            _number = _number * DECIMAL_BASE + static_cast<uint64_t>(letter - '0');
            _in_number = true;
            continue;
        }
        if (_in_number && _count == PAIR_SIZE) { return PairResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        if (_in_number) { _numbers[_count++] = _number; }
        _number = 0;
        _in_number = false;
        if (letter != ']' || _count != PAIR_SIZE) { continue; }
        _current = TagPair{static_cast<uint32_t>(_numbers[0]), _numbers[1]};
        _count = 0;
        return PairResult::success(true);
    }
    if (_count != 0 || _in_number) { return PairResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return PairResult::success(false);
}

}
