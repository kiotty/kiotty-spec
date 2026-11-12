#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class RelationEditor
{
public:
    explicit RelationEditor(Workspace& workspace) : _workspace(workspace) {}

    KiottySpecStatus add(Span<const char> from_id, uint32_t relation, Span<const char> to_id);
    KiottySpecStatus remove(Span<const char> from_id, uint32_t relation, Span<const char> to_id);

private:
    Result<entity::RelationRecord, KiottySpecStatus>
    relationOf(Span<const char> from_id, uint32_t relation, Span<const char> to_id);

    Workspace& _workspace;
};

}
