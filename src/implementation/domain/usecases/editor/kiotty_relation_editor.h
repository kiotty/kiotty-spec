#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class RelationEditor
{
public:
    explicit RelationEditor(Workspace& workspace) : _workspace(workspace) {}

    KiottySpecStatus add(const entity::RelationRecord& relation);
    KiottySpecStatus remove(const entity::RelationRecord& relation);

private:
    Span<const char> idOf(entity::ItemHandle item) const;

    Workspace& _workspace;
};

}
