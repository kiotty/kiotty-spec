#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <algorithm>
#include <cstddef>

namespace kiotty::spec::implementation {

class SpanText
{
public:
    explicit SpanText(Span<char> area) : _area(area)
    {
        const Result<Span<char>, KiottySpecStatus> head = _area.subspan(0, 1);
        clearLetters(head.value);
    }

    KiottySpecStatus append(Span<const char> text)
    {
        const size_t text_length = text.length();
        const Result<Span<char>, KiottySpecStatus> target = _area.subspan(_length, text_length + 1);
        if (!target) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        writeTerminated(target.value, text);
        _length += text_length;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> view() const { return Span<const char>(_area.data(), _length); }
    size_t capacity() const { return measureCapacity(_area.length()); }

private:
    static void writeTerminated(Span<char> target, Span<const char> text)
    {
        const size_t text_length = text.length();
        for (size_t index = 0; index < text_length; ++index) { target[index] = text[index]; }
        target[text_length] = '\0';
    }

    static void clearLetters(Span<char> letters)
    {
        for (char& letter : letters) { letter = '\0'; }
    }

    static size_t measureCapacity(size_t area_length)
    { return std::max(area_length, static_cast<size_t>(1)) - 1; }

    Span<char> _area;
    size_t _length {0};
};

}
