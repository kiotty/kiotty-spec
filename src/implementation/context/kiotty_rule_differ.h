#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_rule_diff_store.h"
#include "implementation/context/kiotty_text_differ.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/usecases/rule_sync/kiotty_rule_compare.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class RuleDiffer
{
public:
    RuleDiffer(Workspace& local, CursorPool& cursors, RuleDiffStore& store)
        : _local(local), _cursors(cursors), _store(store)
    {}

    CursorIdResult open(Workspace& remote);
    Result<RuleDiffRecord, KiottySpecStatus> next(CursorId cursor);
    CursorIdResult openBody(Workspace& remote, Span<const char> id);
    Result<TextPiece, KiottySpecStatus> nextPiece(CursorId cursor);

private:
    KiottySpecStatus checkFree(const std::optional<CursorId>& active, CursorKind kind);
    Result<size_t, KiottySpecStatus> collectRows(RulePair pair);
    KiottySpecStatus pushRows(CursorId cursor, size_t count);
    KiottySpecStatus fillBody(CursorId cursor, TextPair texts);
    KiottySpecStatus storeBodies(TextPair texts);
    KiottySpecStatus pushPieces(CursorId cursor, Span<const TextSegment> segments);

    Workspace& _local;
    CursorPool& _cursors;
    RuleDiffStore& _store;
};

}
