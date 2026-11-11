#include "implementation/data/datasource/kiotty_kisp_line.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char TAB = '\t';
constexpr char ESCAPE = '\\';

// 이스케이프 뒤 글자를 원래 글자로. 모르는 글자면 '\0'
char unescape(char letter)
{
    switch (letter) {
    case 't': return '\t';
    case 'n': return '\n';
    case 'r': return '\r';
    case '\\': return '\\';
    default: return '\0';
    }
}

// 원래 글자를 이스케이프 뒤 글자로. 이스케이프할 글자가 아니면 '\0'
char escapeOf(char letter)
{
    switch (letter) {
    case '\t': return 't';
    case '\n': return 'n';
    case '\r': return 'r';
    case '\\': return '\\';
    default: return '\0';
    }
}

}

Result<KispFields, KiottySpecStatus> KispLineDecoder::decode(Span<const char> line) const
{
    using FieldsResult = Result<KispFields, KiottySpecStatus>;
    KispFields fields {};
    size_t written = 0;
    size_t start = 0;
    size_t index = 0;
    while (index <= line.length()) {
        const bool end = index == line.length() || line[index] == TAB;
        if (end) {
            if (fields.count == MAX_KISP_FIELDS) { return FieldsResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
            fields.values[fields.count] = Span<const char>(_area.data() + start, written - start);
            ++fields.count;
            start = written;
            ++index;
            continue;
        }
        char letter = line[index];
        if (letter == ESCAPE) {
            letter = index + 1 < line.length() ? unescape(line[index + 1]) : '\0';
            if (letter == '\0') { return FieldsResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
            ++index;
        }
        if (written == _area.length()) { return FieldsResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
        _area[written] = letter;
        ++written;
        ++index;
    }
    return FieldsResult::success(fields);
}

void KispLineEncoder::add(Span<const char> field)
{
    if (_count > 0 && _status == KIOTTY_SPEC_STATUS_SUCCESS) { _status = _text.append(literalText("\t")); }
    ++_count;
    extend(field);
}

void KispLineEncoder::extend(Span<const char> text)
{
    for (size_t index = 0; index < text.length() && _status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        const char escaped[2] = {ESCAPE, escapeOf(text[index])};
        const bool plain = escaped[1] == '\0';
        _status = plain ? _text.append(Span<const char>(&text[index], 1)) : _text.append(Span<const char>(escaped, 2));
    }
}

Result<Span<const char>, KiottySpecStatus> KispLineEncoder::finish()
{
    if (_status == KIOTTY_SPEC_STATUS_SUCCESS) { _status = _text.append(literalText("\n")); }
    if (_status != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<Span<const char>, KiottySpecStatus>::failure(_status); }
    return Result<Span<const char>, KiottySpecStatus>::success(_text.view());
}

}
