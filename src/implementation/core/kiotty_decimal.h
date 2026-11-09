#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// number 를 10진수로 text 뒤에 붙인다. 자릿수가 min_digits 보다 적으면 앞을 0 으로 채운다.
// Text 는 append(Span<const char>) 를 가진 FixedString · TextBuffer 다.
template <typename Text>
KiottySpecStatus appendDecimal(Text& text, uint64_t number, size_t min_digits = 1)
{
    constexpr size_t MAX_DIGITS = 20;
    char digits[MAX_DIGITS] {};
    size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + number % 10);
        number /= 10;
        ++count;
    } while (number > 0);
    while (count < min_digits && count < MAX_DIGITS) { digits[count++] = '0'; }
    for (size_t index = count; index > 0; --index) {
        const KiottySpecStatus status = text.append(Span<const char>(&digits[index - 1], 1));
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
