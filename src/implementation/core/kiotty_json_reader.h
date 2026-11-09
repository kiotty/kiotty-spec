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

// 값을 scratch 에 풀지, 형식만 확인하고 건너뛸지
enum class JsonDecode : uint32_t {
    STORE,
    SKIP,
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
    // key 의 정수 값. 정수가 아니면 fallback. 불은 1 · 0, INT64_MAX 를 넘는 수는 64 비트로 접는다.
    int64_t findInteger(Span<const char> key, int64_t fallback, Span<char> scratch);
    // key 의 글자 값. 없거나 글자가 아니면 NOT_FOUND
    Result<Span<const char>, KiottySpecStatus> findText(Span<const char> key, Span<char> scratch);
    void rewind();

private:
    // 값을 쌓는 곳. SKIP 이면 쌓지 않는다.
    class Sink
    {
    public:
        Sink(Span<char> area, JsonDecode decode) : _area(area), _decode(decode) {}
        bool put(char letter);
        bool putCodePoint(uint32_t point);
        Span<const char> view() const { return Span<const char>(_area.data(), _length); }

    private:
        Span<char> _area;
        JsonDecode _decode;
        size_t _length {0};
    };

    // wanted 가 비어 있으면 모든 값을, 아니면 그 키의 값만 scratch 에 푼다.
    Result<bool, KiottySpecStatus> advance(JsonField& field, Span<char> scratch, Span<const char> wanted);
    KiottySpecStatus open();
    KiottySpecStatus separate();
    KiottySpecStatus close();
    KiottySpecStatus readValue(JsonField& field, Sink sink);
    KiottySpecStatus readLiteral(JsonField& field);
    void skipSpace();
    void skipDigits();
    bool accept(char letter);
    Result<Span<const char>, KiottySpecStatus> readKey();
    KiottySpecStatus readString(Sink& sink);
    KiottySpecStatus readEscape(Sink& sink);
    KiottySpecStatus readUnicode(Sink& sink);
    Result<uint32_t, KiottySpecStatus> readHex();
    KiottySpecStatus readNumber(JsonField& field);
    Result<bool, KiottySpecStatus> scanNumber();
    bool scanDigits();
    bool readInteger(JsonField& field, Span<const char> text);
    KiottySpecStatus readComposite(Sink& sink);

    Span<const char> _line;
    size_t _position {0};
    bool _opened {false};
    bool _closed {false};
};

}
