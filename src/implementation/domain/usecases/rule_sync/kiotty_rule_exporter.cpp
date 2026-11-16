#include "implementation/domain/usecases/rule_sync/kiotty_rule_exporter.h"

namespace kiotty::spec::implementation {

KiottySpecStatus RuleExporter::checkDone()
{
    const KiottySpecStatus fresh = _local.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto latest = _local.checkpoints.get(_local.checkpoints.latest());
    const bool is_done = latest && (*latest.value).state == entity::CheckpointState::DONE;
    if (is_done) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _local.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT);
}

KiottySpecStatus RuleExporter::exportTo(Span<const char> absolute_path)
{
    return _local.file.exportRules(absolute_path, _local.checkpoints.latest());
}

}
