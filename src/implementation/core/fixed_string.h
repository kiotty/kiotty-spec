#pragma once

#include "kiotty_spec.h"
#include "span.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

template <size_t Capacity>
class FixedString
{
public:
    KiottySpecStatus assign(Span<const char> text)
    {
        clear();
        return append(text);
    }

    KiottySpecStatus append(Span<const char> text)
    {
        if (text.length() > Capacity - _length) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < text.length(); ++index) { _data[_length + index] = text[index]; }
        _length += text.length();
        _data[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear()
    {
        _length = 0;
        _data[0] = '\0';
    }

    bool empty() const { return _length == 0; }
    size_t length() const { return _length; }
    static constexpr size_t capacity() { return Capacity; }
    const char* c_str() const { return _data; }
    Span<const char> view() const { return Span<const char>(_data, _length); }
    operator std::string_view() const { return std::string_view(_data, _length); }

private:
    char _data[Capacity + 1] {};
    size_t _length {0};
};

template <size_t Left, size_t Right>
bool operator==(const FixedString<Left>& left, const FixedString<Right>& right)
{
    return std::string_view(left) == std::string_view(right);
}

template <size_t Capacity>
bool operator==(const FixedString<Capacity>& left, std::string_view right) { return std::string_view(left) == right; }

template <size_t Capacity>
bool operator==(std::string_view left, const FixedString<Capacity>& right) { return left == std::string_view(right); }

template <size_t Capacity>
bool operator!=(const FixedString<Capacity>& left, std::string_view right) { return std::string_view(left) != right; }

template <size_t Capacity>
std::string operator+(std::string left, const FixedString<Capacity>& right)
{
    left.append(right.c_str(), right.length());
    return left;
}

template <size_t Capacity>
std::string operator+(const char* left, const FixedString<Capacity>& right)
{
    return std::string(left) + right;
}

template <size_t Capacity>
std::string operator+(const FixedString<Capacity>& left, std::string_view right)
{
    std::string text(left.c_str(), left.length());
    text.append(right.data(), right.size());
    return text;
}

template <size_t Capacity>
std::string operator+(const FixedString<Capacity>& left, const char* right)
{
    return left + std::string_view(right);
}

}
