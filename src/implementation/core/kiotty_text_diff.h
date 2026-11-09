#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DIFF_TEXT_BYTES = 255;

enum class TextChange : uint8_t { UNCHANGED = 0, ADDED = 1, REMOVED = 2 };
enum class TextSource : uint8_t { BEFORE = 0, AFTER = 1 };

// 글자 diff 조각. source 글자의 [offset, offset + length) 바이트다.
struct TextSegment {
    TextChange change;
    TextSource source;
    uint16_t offset;
    uint16_t length;
    uint16_t reserved;
};

static_assert(sizeof(TextSegment) == 8, "unexpected padding");

// 설계서 v1 5.4 글자 diff. UTF-8 글자 단위 LCS 로 비교한 뒤,
// 편집 사이의 짧은 공통 구간은 편집에 합치고 편집을 공백 경계까지 넓혀 낱말 단위로 읽히게 한다.
// 같은 자리의 편집은 REMOVED 다음 ADDED 다. table 은 (글자 수 + 1)² 바이트 이상이어야 한다.
// 값은 out 에 쓴 조각 수. 입력이 255 바이트를 넘거나 table · out 이 작으면 CAPACITY_EXCEEDED
Result<size_t, KiottySpecStatus> diffText(Span<const char> before, Span<const char> after, Span<uint8_t> table, Span<TextSegment> out);

}
