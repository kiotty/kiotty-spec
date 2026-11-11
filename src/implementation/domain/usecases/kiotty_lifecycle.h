#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class Lifecycle
{
public:
    Lifecycle(Workspace& workspace, Checker& checker) : _workspace(workspace), _checker(checker) {}

    Result<uint32_t, KiottySpecStatus> beginWrite();

    Result<uint32_t, KiottySpecStatus> start(entity::Step step, Span<const char> description);
    KiottySpecStatus resume();
    KiottySpecStatus complete(KiottySpecActor actor);
    Result<size_t, KiottySpecStatus> prune(KiottySpecActor actor, uint32_t base);

private:
    KiottySpecStatus requireUser(KiottySpecActor actor);

    Workspace& _workspace;
    Checker& _checker;
};

}
