#pragma once

#include "kiotty_spec.h"
#include "kiotty_fixed_string.h"
#include "kiotty_span.h"

#include <cstddef>
#include <initializer_list>

namespace kiotty::spec::implementation {

constexpr size_t MAX_FAILURE_MESSAGE_LENGTH = 4096;

class Failure
{
public:
    KiottySpecStatus raise(KiottySpecStatus status, std::initializer_list<Span<const char>> parts)
    {
        _message.clear();
        for (const Span<const char> part : parts) { appendBounded(part); }
        return status;
    }

    Span<const char> message() const { return _message.view(); }

private:
    void appendBounded(Span<const char> part)
    {
        const size_t room = _message.capacity() - _message.length();
        const size_t length = part.length() < room ? part.length() : room;
        _message.append(Span<const char>(part.data(), length));
    }

    FixedString<MAX_FAILURE_MESSAGE_LENGTH> _message;
};

}
