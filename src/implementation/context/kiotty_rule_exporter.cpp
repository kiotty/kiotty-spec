#include "implementation/context/kiotty_rule_exporter.h"

namespace kiotty::spec::implementation {

KiottySpecStatus RuleExporter::exportTo(Span<const char> path)
{
    const bool is_empty = path.length() == 0;
    if (is_empty) {
        return _local.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE);
    }
    const KiottySpecStatus done = checkDone();
    if (done != KIOTTY_SPEC_STATUS_SUCCESS) { return done; }
    const SystemPathResult absolute = _paths.resolveAbsolutePath(path);
    if (!absolute) { return _local.failure.raise(absolute.code, KIOTTY_SPEC_SUBJECT_FILE, path); }
    const Span<const char> target = (*absolute.value).view();
    const bool is_folder = _files.isDirectory(target);
    if (is_folder) {
        return _local.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, KIOTTY_SPEC_SUBJECT_FILE,
                                    path);
    }
    return _local.file.exportRules(target, _local.checkpoints.latest());
}

KiottySpecStatus RuleExporter::checkDone()
{
    const KiottySpecStatus fresh = _local.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const auto latest = _local.checkpoints.get(_local.checkpoints.latest());
    const bool is_done = latest && (*latest.value).state == entity::CheckpointState::DONE;
    if (is_done) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _local.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, KIOTTY_SPEC_SUBJECT_CHECKPOINT);
}

}
