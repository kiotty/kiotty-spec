#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/entity/kiotty_model.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

constexpr std::string_view META_HEADER = "TABLE\tmeta\tO/X\tkey\tvalue\n";
constexpr std::string_view CHECKPOINT_HEADER =
    "TABLE\tcheckpoint\tO/X\tsequence\tprevious\tstep\tstate\tcreated_at\tdescription\n";
constexpr std::string_view ITEM_HEADER = "TABLE\titem\tO/X\tkey\tid\n";
constexpr std::string_view ITEM_VERSION_HEADER =
    "TABLE\titem_version\tO/X\tkey\tcheckpoint\tkind\tparent\ttitle\tsummary\tbody\n";
constexpr std::string_view TAG_HEADER = "TABLE\ttag\tO/X\tkey\ttag\tcheckpoint\tvalue\n";
constexpr std::string_view RELATION_HEADER =
    "TABLE\trelation\tO/X\tfrom\tmodel\trelation\tto\tcheckpoint\n";
constexpr std::string_view DECISION_HEADER = "TABLE\tdecision\tO/X\tkind\tcheckpoint\ttext\n";
constexpr std::string_view BODY_HEADER = "TABLE\tbody\tO/X\tkey\ttext\n";

class SpecLine
{
public:
    SpecLine(KispLineEncoder line, std::string_view table, uint32_t removed);

    void add(Span<const char> field) { _line.add(field); }
    void addDecimal(uint64_t number);
    void addKey(uint32_t key);
    void addHash(uint64_t hash);
    void addTagValue(Span<const entity::TagName> bit_names, uint64_t value);
    Result<Span<const char>, KiottySpecStatus> finish() { return _line.finish(); }

private:
    KispLineEncoder _line;
};

}
