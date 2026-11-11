#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_span_text.h"

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t MAX_KISP_FIELDS = 10;

// .kisp 한 줄의 필드. 값은 KispLineDecoder 의 영역을 가리킨다.
struct KispFields
{
    Span<const char> values[MAX_KISP_FIELDS] {};
    size_t count {0};
};

// 탭으로 나누고 \t \n \r \\ 를 풀어 area 에 둔다. 모르는 이스케이프나 필드가 너무 많으면 INVALID_ARGUMENT
class KispLineDecoder
{
public:
    explicit KispLineDecoder(Span<char> area) : _area(area) {}

    Result<KispFields, KiottySpecStatus> decode(Span<const char> line) const;

private:
    Span<char> _area;
};

// 필드를 탭으로 잇고 탭 · 줄바꿈 · 역슬래시를 이스케이프해 area 에 한 줄을 쌓는다. 줄마다 새로 만든다.
class KispLineEncoder
{
public:
    explicit KispLineEncoder(Span<char> area) : _text(area) {}

    // 새 필드를 시작한다.
    void add(Span<const char> field);
    // 지금 필드 뒤에 이어 쓴다.
    void extend(Span<const char> text);
    // 줄바꿈을 붙인 줄. 중간에 넘쳤으면 CAPACITY_EXCEEDED
    Result<Span<const char>, KiottySpecStatus> finish();

private:
    SpanText _text;
    size_t _count {0};
    KiottySpecStatus _status {KIOTTY_SPEC_STATUS_SUCCESS};
};

}
