#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/workspace/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class WriteScope
{
public:
    explicit WriteScope(Workspace& workspace)
        : _workspace(workspace), _sequence(beginWrite())
    {}

    WriteScope(const WriteScope&) = delete;
    WriteScope& operator=(const WriteScope&) = delete;

    Result<uint32_t, KiottySpecStatus> sequence() const { return _sequence; }
    KiottySpecStatus finish(KiottySpecStatus status)
    { return _workspace.finish(_sequence ? status : _sequence.code); }

private:
    Result<uint32_t, KiottySpecStatus> beginWrite()
    {
        using SequenceResult = Result<uint32_t, KiottySpecStatus>;
        const KiottySpecStatus fresh = _workspace.refresh();
        if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(fresh); }
        const uint32_t latest = _workspace.checkpoints.latest();
        const auto found = _workspace.checkpoints.get(latest);
        if (!found) {
            return SequenceResult::failure(_workspace.failure.raise(
                KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
        }
        const entity::CheckpointRecord& record = *found.value;
        if (record.state == entity::CheckpointState::DONE) {
            return SequenceResult::failure(_workspace.failure.raise(
                KIOTTY_SPEC_STATUS_CONFIRMED, KIOTTY_SPEC_SUBJECT_CHECKPOINT));
        }
        if (record.state == entity::CheckpointState::STARTED) {
            const KiottySpecStatus running = _workspace.checkpoints.writer().setState(
                latest, entity::CheckpointState::RUNNING);
            if (running != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(running); }
        }
        return SequenceResult::success(latest);
    }

    Workspace& _workspace;
    Result<uint32_t, KiottySpecStatus> _sequence;
};

}
