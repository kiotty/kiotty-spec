#include "implementation/data/datasource/kiotty_kisp_line.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char TAB = '\t';
constexpr char ESCAPE = '\\';

struct EscapePair {
    char original {'\0'};
    char escaped {'\0'};
};

static_assert(sizeof(EscapePair) == 2, "unexpected padding");

constexpr EscapePair ESCAPE_PAIRS[] = {{'\t', 't'}, {'\n', 'n'}, {'\r', 'r'}, {'\\', '\\'}};

constexpr size_t ESCAPE_SEQUENCE_LENGTH = 2;

char unescape(char letter);
char escapeOf(char letter);

}

Result<KispFields, KiottySpecStatus> KispLineDecoder::decode(Span<const char> line) const
{
    using FieldsResult = Result<KispFields, KiottySpecStatus>;
    KispFields fields {};
    size_t written = 0;
    size_t start = 0;
    size_t index = 0;
    const size_t length = line.length();
    const size_t capacity = _area.length();
    while (index <= length) {
        const bool end = index == length || line[index] == TAB;
        if (end) {
            if (fields.count == MAX_KISP_FIELDS) {
                return FieldsResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
            }
            fields.values[fields.count] = Span<const char>(_area.data() + start, written - start);
            ++fields.count;
            start = written;
            ++index;
            continue;
        }
        char letter = line[index];
        if (letter == ESCAPE) {
            letter = index + 1 < length ? unescape(line[index + 1]) : '\0';
            if (letter == '\0') {
                return FieldsResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
            }
            ++index;
        }
        if (written == capacity) {
            return FieldsResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
        }
        _area[written] = letter;
        ++written;
        ++index;
    }
    return FieldsResult::success(fields);
}

void KispLineEncoder::add(Span<const char> field)
{
    if (_count > 0 && _status == KIOTTY_SPEC_STATUS_SUCCESS) {
        _status = _text.append(literalText("\t"));
    }
    ++_count;
    extend(field);
}

void KispLineEncoder::extend(Span<const char> text)
{
    const size_t length = text.length();
    for (size_t index = 0; index < length && _status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        const char escaped[ESCAPE_SEQUENCE_LENGTH] = {ESCAPE, escapeOf(text[index])};
        const bool plain = escaped[1] == '\0';
        _status = plain ? _text.append(Span<const char>(&text[index], 1))
                        : _text.append(Span<const char>(escaped, ESCAPE_SEQUENCE_LENGTH));
    }
}

Result<Span<const char>, KiottySpecStatus> KispLineEncoder::finish()
{
    if (_status == KIOTTY_SPEC_STATUS_SUCCESS) { _status = _text.append(literalText("\n")); }
    if (_status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return Result<Span<const char>, KiottySpecStatus>::failure(_status);
    }
    return Result<Span<const char>, KiottySpecStatus>::success(_text.view());
}

namespace {

char unescape(char letter)
{
    for (const EscapePair& pair : ESCAPE_PAIRS) {
        if (pair.escaped == letter) { return pair.original; }
    }
    return '\0';
}

char escapeOf(char letter)
{
    for (const EscapePair& pair : ESCAPE_PAIRS) {
        if (pair.original == letter) { return pair.escaped; }
    }
    return '\0';
}

}

}
