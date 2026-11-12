#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_utf8.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation::entity {

constexpr size_t MAX_FAILURE_TEXT_LENGTH = KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH;

using FailureText = FixedString<MAX_FAILURE_TEXT_LENGTH>;

struct FailureDetail {
    FailureText text {};
    uint64_t line {0};
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
    KiottySpecSubject subject {KIOTTY_SPEC_SUBJECT_NONE};
};

static_assert(sizeof(FailureDetail) == 272, "unexpected padding");

inline Span<const char> cutAtCharacter(Span<const char> text, size_t limit)
{
    const size_t text_length = text.length();
    if (text_length <= limit) { return text; }
    size_t length {limit};
    for (size_t index = limit; index > 0; --index) {
        const bool is_continuation = isUtf8Continuation(text[index]);
        if (!is_continuation) { break; }
        --length;
    }
    return text.subspan(0, length).value;
}

inline uint64_t keepFirstLine(uint64_t current_line, uint64_t line)
{
    const bool has_line = current_line != 0;
    return has_line ? current_line : line;
}

class Failure
{
public:
    KiottySpecStatus raise(KiottySpecStatus status, KiottySpecSubject subject,
                           Span<const char> text = Span<const char>(), uint64_t line = 0)
    {
        _detail.status = status;
        _detail.subject = subject;
        _detail.line = line;
        _detail.text.assign(cutAtCharacter(text, MAX_FAILURE_TEXT_LENGTH));
        return status;
    }

    KiottySpecStatus locate(KiottySpecStatus status, uint64_t line)
    {
        _detail.line = keepFirstLine(_detail.line, line);
        return status;
    }

    const FailureDetail& detail() const { return _detail; }

private:
    FailureDetail _detail {};
};

}
