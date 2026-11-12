#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/checker/kiotty_checker.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class Lifecycle
{
public:
    Lifecycle(Workspace& workspace, Checker& checker) : _workspace(workspace), _checker(checker) {}

    Result<uint32_t, KiottySpecStatus> start(entity::Step step, Span<const char> description);
    KiottySpecStatus resume();
    KiottySpecStatus complete(entity::Actor actor);
    Result<size_t, KiottySpecStatus> prune(entity::Actor actor, uint32_t base);

private:
    KiottySpecStatus requireUser(entity::Actor actor);

    Workspace& _workspace;
    Checker& _checker;
};

}
