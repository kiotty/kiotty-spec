#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class Editor
{
public:
    Editor(Workspace& workspace, Lifecycle& lifecycle) : _w(workspace), _lifecycle(lifecycle) {}

    Result<entity::IdText, KiottySpecStatus> createItem(entity::ItemKind kind, Span<const char> id, Span<const char> title, Span<const char> summary);
    KiottySpecStatus renameItem(Span<const char> id, Span<const char> new_id);
    KiottySpecStatus updateItem(Span<const char> id, Span<const char> title, Span<const char> summary);
    KiottySpecStatus setParent(Span<const char> id, Span<const char> parent_id);
    KiottySpecStatus removeItem(Span<const char> id);
    KiottySpecStatus writeBody(Span<const char> id, Span<const char> body);
    KiottySpecStatus setTag(entity::ItemHandle item, uint32_t tag, uint64_t value);
    KiottySpecStatus writeDecision(entity::ItemKind kind, Span<const char> text);

private:
    Result<entity::IdText, KiottySpecStatus> insertItem(entity::ItemKind kind, Span<const char> id, Span<const char> title, Span<const char> summary,
                                                        uint32_t sequence);
    KiottySpecStatus checkId(Span<const char> id);
    KiottySpecStatus markNeedsChange(entity::ItemHandle item, uint32_t sequence);

    Workspace& _w;
    Lifecycle& _lifecycle;
};

}
