#pragma once

#include "implementation/core/kiotty_span.h"

namespace kiotty::spec::implementation {

// jsonl 가져오기가 작업 버퍼를 나눠 쓰는 영역. 줄 · 짧은 값 둘 · 긴 값
struct ImportAreas {
    Span<char> line;
    Span<char> first;
    Span<char> second;
    Span<char> text;
};

}
