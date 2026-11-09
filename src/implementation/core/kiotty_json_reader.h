#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

enum class JsonType : uint32_t {
    NULL_VALUE,
    BOOLEAN,
    INTEGER,
    // INT64_MAX 보다 큰 양수. natural 에 담는다.
    UNSIGNED,
    REAL,
    TEXT,
    // 배열 · 객체. text 에 공백을 뺀 원문이 담긴다.
    COMPOSITE,
};

struct JsonField {
    Span<const char> key;
    JsonType type {JsonType::NULL_VALUE};
    bool boolean {false};
    int64_t integer {0};
    uint64_t natural {0};
    double real {0.0};
    Span<const char> text;
};

// jsonl 한 줄(객체 하나)의 맨 위 필드를 차례로 읽는다. 힙을 쓰지 않는다.
// 글자 값은 이스케이프를 풀어 scratch 에 쓰고, field.text 가 그곳을 가리킨다. 다음 호출이 scratch 를 덮어쓴다.
// 키에는 이스케이프를 쓸 수 없다 (열 이름만 온다).
class JsonReader
{
public:
    explicit JsonReader(Span<const char> line) : _line(line) {}

    // 다음 필드가 있으면 true. 형식이 어긋나면 INVALID_ARGUMENT, scratch 가 모자라면 CAPACITY_EXCEEDED
    Result<bool, KiottySpecStatus> next(JsonField& field, Span<char> scratch);
    // 처음부터 다시 훑어 key 를 찾는다. 없으면 false
    Result<bool, KiottySpecStatus> find(Span<const char> key, JsonField& field, Span<char> scratch);
    void rewind();

private:
    // wanted 가 있으면 그 키의 값만 scratch 에 푼다.
    Result<bool, KiottySpecStatus> advance(JsonField& field, Span<char> scratch, const Span<const char>* wanted);
    void skipSpace();
    KiottySpecStatus readString(Span<char> scratch, Span<const char>& out, bool store);
    KiottySpecStatus readKey(Span<const char>& out);
    KiottySpecStatus readNumber(JsonField& field);
    KiottySpecStatus readComposite(Span<char> scratch, Span<const char>& out, bool store);

    Span<const char> _line;
    size_t _position {0};
    bool _opened {false};
    bool _closed {false};
};

}
