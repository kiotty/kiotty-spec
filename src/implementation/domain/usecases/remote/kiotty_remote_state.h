#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_remote_record.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <string_view>

namespace kiotty::spec::implementation {

class RemoteState
{
public:
    explicit RemoteState(Workspace& workspace) : _workspace(workspace) {}

    Result<entity::RemoteRecord, KiottySpecStatus> read() const;
    KiottySpecStatus write(const entity::RemoteRecord& remote);
    KiottySpecStatus writeBase(Span<const char> rule_base);

private:
    KiottySpecStatus storeMeta(std::string_view key, Span<const char> value);

    Workspace& _workspace;
};

}
