#pragma once

#include "implementation/domain/entity/kiotty_diff_records.h"
#include "kiotty_spec.h"
#include "implementation/data/datasource/cursor/kiotty_cursor_pool.h"
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

struct DiffCursors {
    std::optional<CursorId> rule {};
    std::optional<CursorId> body {};
};

static_assert(sizeof(DiffCursors) == 32, "unexpected padding");

class RuleDiffStore
{
public:
    RuleDiffStore();

    Span<entity::RuleDiffRecord> rows() { return viewVector(_rows); }
    Result<Span<char>, KiottySpecStatus> textOf(TextSource source);
    Span<TextSegment> segments() { return viewVector(_segments); }
    LineWork lineWork();
    DiffCursors& active() { return _active; }

private:
    std::vector<entity::RuleDiffRecord> _rows;
    std::vector<char> _texts;
    std::vector<TextSegment> _segments;
    std::vector<uint8_t> _moves;
    std::vector<uint16_t> _lengths;
    std::vector<uint16_t> _starts;
    DiffCursors _active {};
};

}
