#pragma once

#include "kiotty_spec.h"
#include "span.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

// 길이를 따로 두지 않고 null 종료로 끝을 표시한다. sizeof == Capacity + 1 이라
// 레코드 안에 그대로 넣어도 크기가 설계서의 바이트 수와 같다.
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
        const size_t current = length();
        if (text.length() > Capacity - current) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < text.length(); ++index) { _data[current + index] = text[index]; }
        _data[current + text.length()] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    void clear() { _data[0] = '\0'; }

    bool empty() const { return _data[0] == '\0'; }
    size_t length() const
    {
        size_t count = 0;
        while (count < Capacity && _data[count] != '\0') { ++count; }
        return count;
    }
    static constexpr size_t capacity() { return Capacity; }
    const char* c_str() const { return _data; }
    Span<const char> view() const { return Span<const char>(_data, length()); }
    operator std::string_view() const { return std::string_view(_data, length()); }

private:
    char _data[Capacity + 1] {};
};

template <size_t Left, size_t Right>
bool operator<(const FixedString<Left>& left, const FixedString<Right>& right)
{
    return std::string_view(left) < std::string_view(right);
}

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
