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

struct FieldCursor {
    size_t index {0};
    size_t written {0};
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
};

static_assert(sizeof(FieldCursor) == 24, "unexpected padding");

constexpr EscapePair ESCAPE_PAIRS[] = {{'\t', 't'}, {'\n', 'n'}, {'\r', 'r'}, {'\\', '\\'}};

constexpr size_t ESCAPE_SEQUENCE_LENGTH = 2;

Result<KispFields, KiottySpecStatus> fillFields(Span<const char> line, Span<char> area);
FieldCursor writeField(Span<const char> line, Span<char> area, const FieldCursor& initial);
char readLetter(Span<const char> text, size_t index);
bool isSeparatorDue(size_t count, KiottySpecStatus status);
bool isSuccess(KiottySpecStatus status);
char unescape(char letter);
char escapeOf(char letter);

}

Result<KispFields, KiottySpecStatus> KispLineDecoder::decode(Span<const char> line) const
{ return fillFields(line, _area); }

void KispLineEncoder::add(Span<const char> field)
{
    const bool is_separator_due = isSeparatorDue(_count, _status);
    if (is_separator_due) { _status = _text.append(literalText("\t")); }
    ++_count;
    extend(field);
}

void KispLineEncoder::extend(Span<const char> text)
{
    const size_t length = text.length();
    for (size_t index = 0; index < length; ++index) {
        const char escaped_letter = escapeOf(text[index]);
        const char sequence[ESCAPE_SEQUENCE_LENGTH] = {ESCAPE, escaped_letter};
        const bool is_plain = escaped_letter == '\0';
        const Span<const char> escaped(sequence, ESCAPE_SEQUENCE_LENGTH);
        const Span<const char> piece = is_plain ? Span<const char>(&text[index], 1) : escaped;
        const bool is_writing = isSuccess(_status);
        if (is_writing) { _status = _text.append(piece); }
    }
}

Result<Span<const char>, KiottySpecStatus> KispLineEncoder::finish()
{
    const bool is_writing = isSuccess(_status);
    if (is_writing) { _status = _text.append(literalText("\n")); }
    const bool is_finished = isSuccess(_status);
    if (is_finished) { return Result<Span<const char>, KiottySpecStatus>::success(_text.view()); }
    return Result<Span<const char>, KiottySpecStatus>::failure(_status);
}

namespace {

Result<KispFields, KiottySpecStatus> fillFields(Span<const char> line, Span<char> area)
{
    using FieldsResult = Result<KispFields, KiottySpecStatus>;
    KispFields fields {};
    const Span<Span<const char>> slots(fields.values, MAX_KISP_FIELDS);
    FieldCursor cursor {};
    const size_t length = line.length();
    while (cursor.index <= length) {
        const size_t start = cursor.written;
        cursor = writeField(line, area, cursor);
        if (cursor.status != KIOTTY_SPEC_STATUS_SUCCESS) {
            return FieldsResult::failure(cursor.status);
        }
        const Result<Span<const char>&, KiottySpecStatus> slot = slots.at(fields.count);
        if (!slot) { return FieldsResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        const Result<Span<char>, KiottySpecStatus> field =
            area.subspan(start, cursor.written - start);
        *slot.value = field.value;
        ++fields.count;
        ++cursor.index;
    }
    return FieldsResult::success(fields);
}

FieldCursor writeField(Span<const char> line, Span<char> area, const FieldCursor& initial)
{
    FieldCursor cursor = initial;
    const size_t length = line.length();
    while (cursor.index < length) {
        const char letter = readLetter(line, cursor.index);
        if (letter == TAB) { return cursor; }
        const bool is_escape = letter == ESCAPE;
        const char decoded = is_escape ? unescape(readLetter(line, cursor.index + 1)) : letter;
        const bool is_broken_escape = is_escape && decoded == '\0';
        const Result<char&, KiottySpecStatus> target = area.at(cursor.written);
        if (is_broken_escape) {
            cursor.status = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
            return cursor;
        }
        if (!target) {
            cursor.status = KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
            return cursor;
        }
        *target.value = decoded;
        ++cursor.written;
        cursor.index += is_escape ? ESCAPE_SEQUENCE_LENGTH : 1;
    }
    return cursor;
}

char readLetter(Span<const char> text, size_t index)
{
    const Result<const char&, KiottySpecStatus> letter = text.at(index);
    return letter ? *letter.value : '\0';
}

bool isSeparatorDue(size_t count, KiottySpecStatus status)
{ return count > 0 && status == KIOTTY_SPEC_STATUS_SUCCESS; }

bool isSuccess(KiottySpecStatus status) { return status == KIOTTY_SPEC_STATUS_SUCCESS; }

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
