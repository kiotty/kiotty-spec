#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct ItemVersionView {
    entity::ItemLabel label {};
    entity::ItemState state {};
    entity::IdText parent_id {};
};

static_assert(sizeof(ItemVersionView) == 440, "unexpected padding");

class PastReader
{
public:
    explicit PastReader(Workspace& workspace) : _workspace(workspace) {}

    Result<ItemVersionView, KiottySpecStatus> itemAt(Span<const char> id, uint32_t checkpoint);
    Result<uint64_t, KiottySpecStatus> tagAt(Span<const char> id, uint32_t tag,
                                             uint32_t checkpoint);
    Result<size_t, KiottySpecStatus> readBodyAt(Span<const char> id, uint32_t checkpoint,
                                                Span<char> buffer);
    Result<size_t, KiottySpecStatus> readDecisionAt(entity::ItemKind kind, uint32_t checkpoint,
                                                    Span<char> buffer);

private:
    Result<const entity::ItemVersion&, KiottySpecStatus> versionAt(Span<const char> id,
                                                                   uint32_t checkpoint);

    Workspace& _workspace;
};

}
