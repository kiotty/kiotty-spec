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

KiottySpecStatus OpenedContext::initialize(Span<const char> name)
{
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    const Span<const char> project = name.length() > 0 ? name : fileNameOf(_root.view());
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

}
