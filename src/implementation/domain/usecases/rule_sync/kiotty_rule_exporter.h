#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

namespace kiotty::spec::implementation {

class RuleExporter
{
public:
    explicit RuleExporter(Workspace& local) : _local(local) {}

    KiottySpecStatus checkDone();
    KiottySpecStatus exportTo(Span<const char> absolute_path);

private:
    Workspace& _local;
};

}
