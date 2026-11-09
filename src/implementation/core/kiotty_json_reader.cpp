#include "kiotty_json_reader.h"

#include <charconv>
#include <iterator>
#include <limits>
#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using TextResult = Result<Span<const char>, KiottySpecStatus>;
using NextResult = Result<bool, KiottySpecStatus>;

constexpr size_t HEX_DIGITS = 4;
constexpr uint32_t HIGH_SURROGATE_FIRST = 0xD800;
constexpr uint32_t LOW_SURROGATE_FIRST = 0xDC00;
constexpr uint32_t LOW_SURROGATE_LAST = 0xDFFF;
constexpr uint32_t SUPPLEMENTARY_FIRST = 0x10000;
constexpr uint32_t SURROGATE_BITS = 10;

bool isSpace(char letter) { return letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n'; }

bool isDigit(char letter) { return letter >= '0' && letter <= '9'; }

int hexValue(char letter)
{
    if (letter >= '0' && letter <= '9') { return letter - '0'; }
    if (letter >= 'a' && letter <= 'f') { return letter - 'a' + 10; }
    if (letter >= 'A' && letter <= 'F') { return letter - 'A' + 10; }
    return -1;
}

bool sameText(Span<const char> left, Span<const char> right)
{
    return std::string_view(left.data(), left.length()) == std::string_view(right.data(), right.length());
}

// '\n' 같은 한 글자 이스케이프. 'u' 와 모르는 글자는 '\0'
char simpleEscape(char kind)
{
    switch (kind) {
    case '"': return '"';
    case '\\': return '\\';
    case '/': return '/';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    default: return '\0';
    }
}

}

bool JsonReader::Sink::put(char letter)
{
    if (_decode == JsonDecode::SKIP) { return true; }
    if (_length == _area.length()) { return false; }
    _area[_length++] = letter;
    return true;
}

bool JsonReader::Sink::putCodePoint(uint32_t point)
{
    if (point < 0x80) { return put(static_cast<char>(point)); }
    if (point < 0x800) { return put(static_cast<char>(0xC0 | (point >> 6))) && put(static_cast<char>(0x80 | (point & 0x3F))); }
    if (point < SUPPLEMENTARY_FIRST) {
        return put(static_cast<char>(0xE0 | (point >> 12))) && put(static_cast<char>(0x80 | ((point >> 6) & 0x3F))) &&
               put(static_cast<char>(0x80 | (point & 0x3F)));
    }
    return put(static_cast<char>(0xF0 | (point >> 18))) && put(static_cast<char>(0x80 | ((point >> 12) & 0x3F))) &&
           put(static_cast<char>(0x80 | ((point >> 6) & 0x3F))) && put(static_cast<char>(0x80 | (point & 0x3F)));
}

void JsonReader::rewind()
{
    _position = 0;
    _opened = false;
    _closed = false;
}

Result<bool, KiottySpecStatus> JsonReader::next(JsonField& field, Span<char> scratch) { return advance(field, scratch, Span<const char>()); }

Result<bool, KiottySpecStatus> JsonReader::find(Span<const char> key, JsonField& field, Span<char> scratch)
{
    rewind();
    NextResult found = advance(field, scratch, key);
    while (found && found.value && !sameText(field.key, key)) { found = advance(field, scratch, key); }
    return found;
}

int64_t JsonReader::findInteger(Span<const char> key, int64_t fallback, Span<char> scratch)
{
    JsonField field;
    const NextResult found = find(key, field, scratch);
    if (!found || !found.value) { return fallback; }
    switch (field.type) {
    case JsonType::INTEGER: return field.integer;
    case JsonType::UNSIGNED: return static_cast<int64_t>(field.natural);
    case JsonType::BOOLEAN: return field.boolean ? 1 : 0;
    case JsonType::NULL_VALUE:
    case JsonType::REAL:
    case JsonType::TEXT:
    case JsonType::COMPOSITE: break;
    }
    return fallback;
}

Result<Span<const char>, KiottySpecStatus> JsonReader::findText(Span<const char> key, Span<char> scratch)
{
    JsonField field;
    const NextResult found = find(key, field, scratch);
    if (!found) { return TextResult::failure(found.code); }
    if (!found.value || field.type != JsonType::TEXT) { return TextResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return TextResult::success(field.text);
}

// wanted 가 비어 있으면 모든 값을, 아니면 그 키의 값만 scratch 에 푼다.
Result<bool, KiottySpecStatus> JsonReader::advance(JsonField& field, Span<char> scratch, Span<const char> wanted)
{
    if (!_opened) {
        const KiottySpecStatus opened = open();
        if (opened != KIOTTY_SPEC_STATUS_SUCCESS) { return NextResult::failure(opened); }
    }
    if (_closed) { return NextResult::success(false); }
    field = JsonField{};
    skipSpace();
    const TextResult key = readKey();
    if (!key) { return NextResult::failure(key.code); }
    field.key = key.value;
    skipSpace();
    if (!accept(':')) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    skipSpace();
    const bool decode = wanted.length() == 0 || sameText(field.key, wanted);
    const KiottySpecStatus read = readValue(field, Sink(scratch, decode ? JsonDecode::STORE : JsonDecode::SKIP));
    if (read != KIOTTY_SPEC_STATUS_SUCCESS) { return NextResult::failure(read); }
    const KiottySpecStatus separated = separate();
    if (separated != KIOTTY_SPEC_STATUS_SUCCESS) { return NextResult::failure(separated); }
    return NextResult::success(true);
}

// '{' 를 읽는다. 빈 객체면 바로 닫는다.
KiottySpecStatus JsonReader::open()
{
    skipSpace();
    if (!accept('{')) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    _opened = true;
    skipSpace();
    if (!accept('}')) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return close();
}

// 값 뒤의 ',' 나 '}'
KiottySpecStatus JsonReader::separate()
{
    skipSpace();
    if (accept(',')) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (accept('}')) { return close(); }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

// '}' 뒤에는 공백만 올 수 있다.
KiottySpecStatus JsonReader::close()
{
    _closed = true;
    skipSpace();
    return _position == _line.length() ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

KiottySpecStatus JsonReader::readValue(JsonField& field, Sink sink)
{
    if (_position >= _line.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const char lead = _line[_position];
    if (lead == '-' || isDigit(lead)) { return readNumber(field); }
    if (lead != '"' && lead != '[' && lead != '{') { return readLiteral(field); }
    field.type = lead == '"' ? JsonType::TEXT : JsonType::COMPOSITE;
    const KiottySpecStatus read = lead == '"' ? readString(sink) : readComposite(sink);
    field.text = sink.view();
    return read;
}

KiottySpecStatus JsonReader::readLiteral(JsonField& field)
{
    const std::string_view rest(_line.data() + _position, _line.length() - _position);
    const std::string_view words[] = {"null", "true", "false"};
    const JsonType types[] = {JsonType::NULL_VALUE, JsonType::BOOLEAN, JsonType::BOOLEAN};
    for (size_t index = 0; index < std::size(words); ++index) {
        if (rest.substr(0, words[index].size()) != words[index]) { continue; }
        field.type = types[index];
        field.boolean = index == 1;
        _position += words[index].size();
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

void JsonReader::skipSpace()
{
    while (_position < _line.length() && isSpace(_line[_position])) { ++_position; }
}

void JsonReader::skipDigits()
{
    while (_position < _line.length() && isDigit(_line[_position])) { ++_position; }
}

bool JsonReader::accept(char letter)
{
    if (_position >= _line.length() || _line[_position] != letter) { return false; }
    ++_position;
    return true;
}

// 키에는 이스케이프를 쓸 수 없다 (열 이름만 온다).
Result<Span<const char>, KiottySpecStatus> JsonReader::readKey()
{
    if (!accept('"')) { return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const size_t start = _position;
    while (_position < _line.length() && _line[_position] != '"') {
        const bool plain = _line[_position] != '\\' && static_cast<unsigned char>(_line[_position]) >= 0x20;
        if (!plain) { return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        ++_position;
    }
    const size_t end = _position;
    if (!accept('"')) { return TextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return TextResult::success(Span<const char>(_line.data() + start, end - start));
}

KiottySpecStatus JsonReader::readString(Sink& sink)
{
    ++_position;
    while (_position < _line.length()) {
        const char letter = _line[_position++];
        if (letter == '"') { return KIOTTY_SPEC_STATUS_SUCCESS; }
        if (static_cast<unsigned char>(letter) < 0x20) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        if (letter == '\\') {
            const KiottySpecStatus escaped = readEscape(sink);
            if (escaped != KIOTTY_SPEC_STATUS_SUCCESS) { return escaped; }
        } else if (!sink.put(letter)) {
            return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
        }
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

KiottySpecStatus JsonReader::readEscape(Sink& sink)
{
    if (_position >= _line.length()) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const char kind = _line[_position++];
    if (kind == 'u') { return readUnicode(sink); }
    const char simple = simpleEscape(kind);
    if (simple == '\0') { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    return sink.put(simple) ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
}

// "\uXXXX". 상위 대리 글자면 이어지는 "\uXXXX" 하위 대리 글자와 합친다.
KiottySpecStatus JsonReader::readUnicode(Sink& sink)
{
    const Result<uint32_t, KiottySpecStatus> high = readHex();
    if (!high || (high.value >= LOW_SURROGATE_FIRST && high.value <= LOW_SURROGATE_LAST)) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    uint32_t point = high.value;
    if (point >= HIGH_SURROGATE_FIRST && point < LOW_SURROGATE_FIRST) {
        if (!accept('\\') || !accept('u')) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        const Result<uint32_t, KiottySpecStatus> low = readHex();
        if (!low || low.value < LOW_SURROGATE_FIRST || low.value > LOW_SURROGATE_LAST) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        point = SUPPLEMENTARY_FIRST + ((point - HIGH_SURROGATE_FIRST) << SURROGATE_BITS) + (low.value - LOW_SURROGATE_FIRST);
    }
    return sink.putCodePoint(point) ? KIOTTY_SPEC_STATUS_SUCCESS : KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
}

Result<uint32_t, KiottySpecStatus> JsonReader::readHex()
{
    using HexResult = Result<uint32_t, KiottySpecStatus>;
    if (_line.length() - _position < HEX_DIGITS) { return HexResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    uint32_t unit = 0;
    for (size_t index = 0; index < HEX_DIGITS; ++index) {
        const int digit = hexValue(_line[_position + index]);
        if (digit < 0) { return HexResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        unit = (unit << 4) | static_cast<uint32_t>(digit);
    }
    _position += HEX_DIGITS;
    return HexResult::success(unit);
}

KiottySpecStatus JsonReader::readNumber(JsonField& field)
{
    const size_t start = _position;
    const NextResult scanned = scanNumber();
    if (!scanned) { return scanned.code; }
    const char* const first = _line.data() + start;
    const char* const last = _line.data() + _position;
    if (!scanned.value && readInteger(field, Span<const char>(first, _position - start))) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    // 소수 · 지수, 또는 64 비트를 넘는 정수
    const std::from_chars_result parsed = std::from_chars(first, last, field.real);
    if (parsed.ptr != last) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    field.type = JsonType::REAL;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// JSON 숫자 문법을 확인하고 끝까지 간다. 값은 소수 · 지수가 있었는지
Result<bool, KiottySpecStatus> JsonReader::scanNumber()
{
    accept('-');
    const size_t digits = _position;
    skipDigits();
    const bool leading_zero = _position - digits > 1 && _line[digits] == '0';
    if (_position == digits || leading_zero) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const bool fraction = accept('.');
    if (fraction && !scanDigits()) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    const bool exponent = accept('e') || accept('E');
    if (exponent && !accept('+')) { accept('-'); }
    if (exponent && !scanDigits()) { return NextResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return NextResult::success(fraction || exponent);
}

// 숫자가 하나 이상 있으면 true
bool JsonReader::scanDigits()
{
    const size_t start = _position;
    skipDigits();
    return _position > start;
}

// 64 비트에 들어가면 true. 음수는 int64, 양수는 uint64 까지
bool JsonReader::readInteger(JsonField& field, Span<const char> text)
{
    const char* const first = text.data();
    const char* const last = text.data() + text.length();
    if (*first == '-') {
        const std::from_chars_result parsed = std::from_chars(first, last, field.integer);
        field.type = JsonType::INTEGER;
        return parsed.ec == std::errc() && parsed.ptr == last;
    }
    const std::from_chars_result parsed = std::from_chars(first, last, field.natural);
    if (parsed.ec != std::errc() || parsed.ptr != last) { return false; }
    const bool fits = field.natural <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    field.type = fits ? JsonType::INTEGER : JsonType::UNSIGNED;
    field.integer = fits ? static_cast<int64_t>(field.natural) : 0;
    return true;
}

// 배열 · 객체. 문자열 밖의 공백을 빼고 원문을 옮긴다.
KiottySpecStatus JsonReader::readComposite(Sink& sink)
{
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    while (_position < _line.length()) {
        const char letter = _line[_position++];
        if (!in_string && isSpace(letter)) { continue; }
        if (!sink.put(letter)) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        const bool quote = letter == '"' && !escaped;
        escaped = in_string && !escaped && letter == '\\';
        in_string = quote ? !in_string : in_string;
        const bool structural = !in_string && !quote;
        const bool opening = structural && (letter == '[' || letter == '{');
        const bool closing = structural && (letter == ']' || letter == '}');
        if (closing && depth == 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
        if (opening) { ++depth; }
        if (closing) { --depth; }
        if (closing && depth == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

}
