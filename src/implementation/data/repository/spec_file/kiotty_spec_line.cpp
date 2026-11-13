#include "implementation/data/repository/spec_file/kiotty_spec_line.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/spec_file/kiotty_spec_tables.h"

namespace kiotty::spec::implementation {

namespace {

constexpr size_t KEY_DIGITS = 8;
constexpr size_t HASH_DIGITS = 16;

}

SpecLine::SpecLine(KispLineEncoder line, std::string_view table, uint32_t removed) : _line(line)
{
    _line.add(spanOf(table));
    _line.add(spanOf(removed != 0 ? REMOVED_MARKER : LIVE_MARKER));
}

void SpecLine::addDecimal(uint64_t number)
{
    const DecimalText text = formatDecimal(number);
    _line.add(text.view());
}

void SpecLine::addKey(uint32_t key)
{
    if (key == entity::NO_KEY) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    const HexText text = formatHex(key, KEY_DIGITS);
    _line.add(text.view());
}

void SpecLine::addHash(uint64_t hash)
{
    if (hash == 0) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    const HexText text = formatHex(hash, HASH_DIGITS);
    _line.add(text.view());
}

void SpecLine::addTagValue(Span<const entity::TagName> bit_names, uint64_t value)
{
    if (value == 0) {
        _line.add(spanOf(NONE_MARKER));
        return;
    }
    _line.add(Span<const char>());
    bool has_written_bit = false;
    for (size_t bit = 0; bit < entity::TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        if (has_written_bit) { _line.extend(literalText(",")); }
        _line.extend(bit_names[bit].view());
        has_written_bit = true;
    }
}

}
