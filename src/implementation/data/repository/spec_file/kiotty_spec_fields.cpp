#include "implementation/data/repository/spec_file/kiotty_spec_fields.h"

#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/domain/entity/kiotty_version_records.h"
#include "implementation/data/repository/spec_file/kiotty_spec_tables.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr size_t KEY_DIGITS = 8;
constexpr size_t HASH_DIGITS = 16;
constexpr uint64_t MAX_SEQUENCE = UINT32_MAX;

bool fits(Span<const char> text, size_t limit);
bool hasNullByte(Span<const char> text);

}

KeyResult parseOptionalKey(const KispFields& fields, size_t index)
{
    const std::string_view value = viewField(fields, index);
    if (value == NONE_MARKER) { return KeyResult::success(entity::NO_KEY); }
    return parseKey(fields, index);
}

KeyResult parseKey(const KispFields& fields, size_t index)
{
    const auto text = readField(fields, index);
    if (!text) { return KeyResult::failure(text.code); }
    const auto number = parseHex(text.value, KEY_DIGITS);
    const bool is_key = number && number.value != entity::NO_KEY;
    if (!is_key) { return KeyResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return KeyResult::success(static_cast<uint32_t>(number.value));
}

KeyResult parseSequence(const KispFields& fields, size_t index)
{
    const auto number = parseNumber(fields, index, MAX_SEQUENCE);
    const bool is_sequence = number && number.value != 0;
    if (!is_sequence) { return KeyResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return KeyResult::success(static_cast<uint32_t>(number.value));
}

NumberResult parseNumber(const KispFields& fields, size_t index, uint64_t maximum)
{
    const auto text = readField(fields, index);
    if (!text) { return NumberResult::failure(text.code); }
    return parseDecimal(text.value, maximum);
}

NumberResult parseHash(const KispFields& fields, size_t index)
{
    const std::string_view value = viewField(fields, index);
    if (value == NONE_MARKER) { return NumberResult::success(0); }
    const auto text = readField(fields, index);
    if (!text) { return NumberResult::failure(text.code); }
    return parseHex(text.value, HASH_DIGITS);
}

Result<ItemKind, KiottySpecStatus> parseKindName(const KispFields& fields, size_t index)
{
    const uint32_t kind = entity::parseKind(viewField(fields, index));
    if (kind == 0) {
        return Result<ItemKind, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    return Result<ItemKind, KiottySpecStatus>::success(static_cast<ItemKind>(kind));
}

FieldResult readText(const KispFields& fields, size_t index, size_t limit)
{
    const auto text = readField(fields, index);
    if (!text) { return text; }
    const bool is_fitting = fits(text.value, limit);
    if (!is_fitting) { return FieldResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return text;
}

std::string_view viewField(const KispFields& fields, size_t index)
{
    const auto text = readField(fields, index);
    if (!text) { return std::string_view(); }
    return viewOf(text.value);
}

FieldResult readField(const KispFields& fields, size_t index)
{
    const Span<const Span<const char>> values(fields.values, fields.count);
    const auto value = values.at(index);
    if (!value) { return FieldResult::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    return FieldResult::success(*value.value);
}

namespace {

bool fits(Span<const char> text, size_t limit)
{
    const bool has_null_byte = hasNullByte(text);
    return text.length() <= limit && !has_null_byte && isValidUtf8(text);
}

bool hasNullByte(Span<const char> text)
{
    for (const char letter : text) {
        if (letter == '\0') { return true; }
    }
    return false;
}

}

}
