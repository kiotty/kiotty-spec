#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class WriteScope
{
public:
    WriteScope(Workspace& workspace, Lifecycle& lifecycle) : _w(workspace), _sequence(lifecycle.beginWrite()) {}

    WriteScope(const WriteScope&) = delete;
    WriteScope& operator=(const WriteScope&) = delete;

    Result<uint32_t, KiottySpecStatus> sequence() const { return _sequence; }
    KiottySpecStatus finish(KiottySpecStatus status) { return _w.finish(_sequence ? status : _sequence.code); }

private:
    Workspace& _w;
    Result<uint32_t, KiottySpecStatus> _sequence;
};

}
