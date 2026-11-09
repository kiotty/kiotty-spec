#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <cstddef>
#include <vector>

namespace kiotty::spec::implementation {

// 초기화 때 크기를 정하는 작업 버퍼. 이후 크기는 바뀌지 않는다 (개발수칙 1.7).
class TextBuffer
{
public:
    explicit TextBuffer(size_t capacity) : _data(capacity + 1, '\0'), _length(0) {}

    void clear()
    {
        _length = 0;
        _data[0] = '\0';
    }

    KiottySpecStatus append(Span<const char> text)
    {
        if (text.length() > capacity() - _length) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        for (size_t index = 0; index < text.length(); ++index) { _data[_length + index] = text[index]; }
        _length += text.length();
        _data[_length] = '\0';
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    Span<const char> view() const { return Span<const char>(_data.data(), _length); }
    size_t capacity() const { return _data.size() - 1; }
    // 내용을 버리고 영역 전체를 작업 공간으로 빌려준다.
    Span<char> scratch()
    {
        clear();
        return Span<char>(_data.data(), capacity());
    }

private:
    std::vector<char> _data;
    size_t _length;
};

}
