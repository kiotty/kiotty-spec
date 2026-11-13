#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/core/kiotty_line_diff.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_diff.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace kiotty::spec::implementation {

constexpr size_t MAX_RULE_DIFF_ROWS = 512;
constexpr size_t BODY_SLOT_BYTES = entity::MAX_BODY_LENGTH + 1;

struct RuleDiffRecord {
    entity::TitleText title {};
    entity::IdText id {};
    uint32_t change {0};
    uint32_t changed_fields {0};
};

struct DiffCursors {
    std::optional<CursorId> rule {};
    std::optional<CursorId> body {};
};

static_assert(sizeof(RuleDiffRecord) == 152, "unexpected padding");
static_assert(sizeof(DiffCursors) == 32, "unexpected padding");

class RuleDiffStore
{
public:
    RuleDiffStore();

    Span<RuleDiffRecord> rows() { return viewVector(_rows); }
    Result<Span<char>, KiottySpecStatus> textOf(TextSource source);
    Span<TextSegment> segments() { return viewVector(_segments); }
    LineWork lineWork();
    DiffCursors& active() { return _active; }

private:
    std::vector<RuleDiffRecord> _rows;
    std::vector<char> _texts;
    std::vector<TextSegment> _segments;
    std::vector<uint8_t> _moves;
    std::vector<uint16_t> _lengths;
    std::vector<uint16_t> _starts;
    DiffCursors _active {};
};

}
