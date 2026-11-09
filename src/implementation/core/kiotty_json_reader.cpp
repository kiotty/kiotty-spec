#include "kiotty_json_reader.h"

#include <charconv>
#include <limits>
#include <string_view>

namespace kiotty::spec::implementation {

namespace {

bool isSpace(char letter) { return letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n'; }

bool isDigit(char letter) { return letter >= '0' && letter <= '9'; }

int hexValue(char letter)
{
    if (letter >= '0' && letter <= '9') { return letter - '0'; }
    if (letter >= 'a' && letter <= 'f') { return letter - 'a' + 10; }
    if (letter >= 'A' && letter <= 'F') { return letter - 'A' + 10; }
    return -1;
}

// code point 를 UTF-8 로 scratch[length] 부터 쓴다.
bool putCodePoint(Span<char> scratch, size_t& length, uint32_t point)
{
    char bytes[4] {};
    size_t count = 0;
    if (point < 0x80) {
        bytes[count++] = static_cast<char>(point);
    } else if (point < 0x800) {
        bytes[count++] = static_cast<char>(0xC0 | (point >> 6));
        bytes[count++] = static_cast<char>(0x80 | (point & 0x3F));
    } else if (point < 0x10000) {
        bytes[count++] = static_cast<char>(0xE0 | (point >> 12));
        bytes[count++] = static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        bytes[count++] = static_cast<char>(0x80 | (point & 0x3F));
    } else {
        bytes[count++] = static_cast<char>(0xF0 | (point >> 18));
        bytes[count++] = static_cast<char>(0x80 | ((point >> 12) & 0x3F));
        bytes[count++] = static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        bytes[count++] = static_cast<char>(0x80 | (point & 0x3F));
    }
    if (count > scratch.length() - length) { return false; }
    for (size_t index = 0; index < count; ++index) { scratch[length++] = bytes[index]; }
    return true;
}

}

void JsonReader::rewind()
{
    _position = 0;
    _opened = false;
    _closed = false;
}

void JsonReader::skipSpace()
{
    while (_position < _line.length() && isSpace(_line[_position])) { ++_position; }
}

Result<bool, KiottySpecStatus> JsonReader::next(JsonField& field, Span<char> scratch) { return advance(field, scratch, nullptr); }

Result<bool, KiottySpecStatus> JsonReader::advance(JsonField& field, Span<char> scratch, const Span<const char>* wanted)
{
    using NextResult = Result<bool, KiottySpecStatus>;
    if (_closed) { return NextResult::success(false); }
    skipSpace();
    if (!_opened) {
        if (_position >= _line.length() || _line[_position] != '{') { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        ++_position;
        _opened = true;
        skipSpace();
        if (_position < _line.length() && _line[_position] == '}') {
            ++_position;
            _closed = true;
            skipSpace();
            if (_position != _line.length()) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
            return NextResult::success(false);
        }
    }
    field = JsonField{};
    KiottySpecStatus status = readKey(field.key);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return NextResult::failure(status); }
    skipSpace();
    if (_position >= _line.length() || _line[_position] != ':') { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    ++_position;
    skipSpace();
    if (_position >= _line.length()) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const char lead = _line[_position];
    // 찾는 키가 아니면 값을 풀지 않고 건너뛴다.
    const bool store = wanted == nullptr || std::string_view(field.key.data(), field.key.length()) == std::string_view(wanted->data(), wanted->length());
    const std::string_view rest(_line.data() + _position, _line.length() - _position);
    if (lead == '"') {
        field.type = JsonType::TEXT;
        status = readString(scratch, field.text, store);
    } else if (lead == '[' || lead == '{') {
        field.type = JsonType::COMPOSITE;
        status = readComposite(scratch, field.text, store);
    } else if (lead == '-' || isDigit(lead)) {
        status = readNumber(field);
    } else if (rest.substr(0, 4) == "null") {
        field.type = JsonType::NULL_VALUE;
        _position += 4;
    } else if (rest.substr(0, 4) == "true") {
        field.type = JsonType::BOOLEAN;
        field.boolean = true;
        _position += 4;
    } else if (rest.substr(0, 5) == "false") {
        field.type = JsonType::BOOLEAN;
        _position += 5;
    } else {
        status = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return NextResult::failure(status); }
    skipSpace();
    if (_position < _line.length() && _line[_position] == ',') {
        ++_position;
        return NextResult::success(true);
    }
    if (_position < _line.length() && _line[_position] == '}') {
        ++_position;
        _closed = true;
        skipSpace();
        if (_position != _line.length()) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        return NextResult::success(true);
    }
    return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
}

Result<bool, KiottySpecStatus> JsonReader::find(Span<const char> key, JsonField& field, Span<char> scratch)
{
    rewind();
    while (true) {
        const auto found = advance(field, scratch, &key);
        if (!found || !found.value) { return found; }
        if (std::string_view(field.key.data(), field.key.length()) == std::string_view(key.data(), key.length())) { return found; }
    }
}

KiottySpecStatus JsonReader::readKey(Span<const char>& out)
{
    if (_position >= _line.length() || _line[_position] != '"') { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const size_t start = ++_position;
    while (_position < _line.length() && _line[_position] != '"') {
        if (_line[_position] == '\\' || static_cast<unsigned char>(_line[_position]) < 0x20) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        ++_position;
    }
    if (_position >= _line.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    out = Span<const char>(_line.data() + start, _position - start);
    ++_position;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus JsonReader::readString(Span<char> scratch, Span<const char>& out, bool store)
{
    ++_position;
    size_t length = 0;
    char sink[4] {};
    // 저장하지 않을 때는 같은 작은 칸에 덮어쓰며 형식만 확인한다.
    if (!store) { scratch = Span<char>(sink, sizeof(sink)); }
    while (_position < _line.length()) {
        const char letter = _line[_position++];
        if (letter == '"') {
            out = store ? Span<const char>(scratch.data(), length) : Span<const char>();
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        if (!store) { length = 0; }
        if (static_cast<unsigned char>(letter) < 0x20) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        if (letter != '\\') {
            if (length == scratch.length()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
            scratch[length++] = letter;
            continue;
        }
        if (_position >= _line.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        const char kind = _line[_position++];
        char simple = '\0';
        switch (kind) {
        case '"': simple = '"'; break;
        case '\\': simple = '\\'; break;
        case '/': simple = '/'; break;
        case 'b': simple = '\b'; break;
        case 'f': simple = '\f'; break;
        case 'n': simple = '\n'; break;
        case 'r': simple = '\r'; break;
        case 't': simple = '\t'; break;
        case 'u': break;
        default: return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
        }
        if (kind != 'u') {
            if (length == scratch.length()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
            scratch[length++] = simple;
            continue;
        }
        auto readHex = [&](uint32_t& unit) {
            if (_line.length() - _position < 4) { return false; }
            unit = 0;
            for (size_t index = 0; index < 4; ++index) {
                const int digit = hexValue(_line[_position + index]);
                if (digit < 0) { return false; }
                unit = (unit << 4) | static_cast<uint32_t>(digit);
            }
            _position += 4;
            return true;
        };
        uint32_t point = 0;
        if (!readHex(point)) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        if (point >= 0xDC00 && point <= 0xDFFF) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        if (point >= 0xD800 && point <= 0xDBFF) {
            uint32_t low = 0;
            if (_line.length() - _position < 2 || _line[_position] != '\\' || _line[_position + 1] != 'u') { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
            _position += 2;
            if (!readHex(low) || low < 0xDC00 || low > 0xDFFF) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
            point = 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00);
        }
        if (!putCodePoint(scratch, length, point)) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

KiottySpecStatus JsonReader::readNumber(JsonField& field)
{
    const size_t start = _position;
    bool negative = false;
    if (_line[_position] == '-') {
        negative = true;
        ++_position;
    }
    const size_t digits = _position;
    while (_position < _line.length() && isDigit(_line[_position])) { ++_position; }
    if (_position == digits) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (_position - digits > 1 && _line[digits] == '0') { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    bool fractional = false;
    if (_position < _line.length() && _line[_position] == '.') {
        fractional = true;
        const size_t after = ++_position;
        while (_position < _line.length() && isDigit(_line[_position])) { ++_position; }
        if (_position == after) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    }
    if (_position < _line.length() && (_line[_position] == 'e' || _line[_position] == 'E')) {
        fractional = true;
        ++_position;
        if (_position < _line.length() && (_line[_position] == '+' || _line[_position] == '-')) { ++_position; }
        const size_t after = _position;
        while (_position < _line.length() && isDigit(_line[_position])) { ++_position; }
        if (_position == after) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    }
    const char* const first = _line.data() + start;
    const char* const last = _line.data() + _position;
    if (!fractional) {
        if (negative) {
            int64_t value = 0;
            const auto parsed = std::from_chars(first, last, value);
            if (parsed.ec == std::errc() && parsed.ptr == last) {
                field.type = JsonType::INTEGER;
                field.integer = value;
                return KIOTTY_SPEC_STATUS_SUCCESS;
            }
        } else {
            uint64_t value = 0;
            const auto parsed = std::from_chars(first, last, value);
            if (parsed.ec == std::errc() && parsed.ptr == last) {
                if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    field.type = JsonType::INTEGER;
                    field.integer = static_cast<int64_t>(value);
                } else {
                    field.type = JsonType::UNSIGNED;
                    field.natural = value;
                }
                return KIOTTY_SPEC_STATUS_SUCCESS;
            }
        }
    }
    // 소수 · 지수, 또는 64 비트를 넘는 정수
    double value = 0.0;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ptr != last) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    field.type = JsonType::REAL;
    field.real = value;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus JsonReader::readComposite(Span<char> scratch, Span<const char>& out, bool store)
{
    size_t length = 0;
    char sink[1] {};
    if (!store) { scratch = Span<char>(sink, sizeof(sink)); }
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    while (_position < _line.length()) {
        const char letter = _line[_position++];
        if (!in_string && isSpace(letter)) { continue; }
        if (!store) { length = 0; }
        if (length == scratch.length()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        scratch[length++] = letter;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (letter == '\\') {
                escaped = true;
            } else if (letter == '"') {
                in_string = false;
            }
            continue;
        }
        if (letter == '"') {
            in_string = true;
        } else if (letter == '[' || letter == '{') {
            ++depth;
        } else if (letter == ']' || letter == '}') {
            if (depth == 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
            if (--depth == 0) {
                out = store ? Span<const char>(scratch.data(), length) : Span<const char>();
                return KIOTTY_SPEC_STATUS_SUCCESS;
            }
        }
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

}
