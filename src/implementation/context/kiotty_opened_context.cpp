#include "implementation/context/kiotty_opened_context.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

KiottySpecStatus ContextWriteSide::completeCheckpoint(entity::Actor actor) const
{
    const auto opened = _lifecycle.openCompletion(actor);
    if (!opened) { return opened.code; }
    const entity::CheckpointRecord& record = *opened.value;
    if (record.state == entity::CheckpointState::DONE) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _lifecycle.complete(_checker.collect(record.step));
}

CursorIdResult ContextReadSide::unmetConditions() const
{
    HistoryFinder history = _parts.finder.history();
    const Result<entity::Step, KiottySpecStatus> step = history.latestStep();
    if (!step) { return CursorIdResult::failure(step.code); }
    return history.listUnmet(_checker.collect(step.value));
}

Result<entity::Position, KiottySpecStatus> ContextReadSide::position() const
{
    const entity::ChangeRequestSummary requests {_checker.openChangeRequestCount(),
                                                 _checker.earliestEntryStep()};
    return _parts.finder.history().locate(requests);
}

KiottySpecStatus OpenedContext::initialize(Span<const char> name)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_paths.root.view());
    KiottySpecStatus status = _workspace.meta.set(literalText("project.name"), project);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _workspace.failure.raise(status, KIOTTY_SPEC_SUBJECT_PROJECT_NAME, project);
    }
    const int64_t seconds = nowEpochSeconds();
    const DecimalText now = formatDecimal(seconds > 0 ? static_cast<uint64_t>(seconds) : 0);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = _workspace.meta.set(literalText("project.created_at"), now.view());
    }
    return _workspace.finish(status);
}

KiottySpecStatus OpenedContext::exportRules(Span<const char> path)
{
    const bool is_empty = path.length() == 0;
    if (is_empty) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_FILE);
    }
    RuleExporter& exporter = _rule_sync.exporter();
    const KiottySpecStatus done = exporter.checkDone();
    if (done != KIOTTY_SPEC_STATUS_SUCCESS) { return done; }
    const SystemPathResult absolute = _paths.resolver.resolveAbsolutePath(path);
    if (!absolute) {
        return _workspace.failure.raise(absolute.code, KIOTTY_SPEC_SUBJECT_FILE, path);
    }
    const Span<const char> target = (*absolute.value).view();
    const bool is_folder = _paths.files.isDirectory(target);
    if (is_folder) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_FILE, path);
    }
    return exporter.exportTo(target);
}

}
