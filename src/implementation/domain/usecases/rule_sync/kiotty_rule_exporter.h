#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/infra/platform/kiotty_system_path.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

namespace kiotty::spec::implementation {

class RuleExporter
{
public:
    RuleExporter(Workspace& local, const FileSystem& files, const PathResolver& paths)
        : _local(local), _files(files), _paths(paths)
    {}

    KiottySpecStatus exportTo(Span<const char> path);

private:
    KiottySpecStatus checkDone();

    Workspace& _local;
    const FileSystem& _files;
    const PathResolver& _paths;
};

}
