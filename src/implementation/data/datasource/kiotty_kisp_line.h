#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_span_text.h"

#include <cstddef>

namespace kiotty::spec::implementation {

constexpr size_t MAX_KISP_FIELDS = 10;

struct KispFields
{
    Span<const char> values[MAX_KISP_FIELDS] {};
    size_t count {0};
};

class KispLineDecoder
{
public:
    explicit KispLineDecoder(Span<char> area) : _area(area) {}

    Result<KispFields, KiottySpecStatus> decode(Span<const char> line) const;

private:
    Span<char> _area;
};

class KispLineEncoder
{
public:
    explicit KispLineEncoder(Span<char> area) : _text(area) {}

    void add(Span<const char> field);
    void extend(Span<const char> text);
    Result<Span<const char>, KiottySpecStatus> finish();

private:
    SpanText _text;
    size_t _count {0};
    KiottySpecStatus _status {KIOTTY_SPEC_STATUS_SUCCESS};
};

}
