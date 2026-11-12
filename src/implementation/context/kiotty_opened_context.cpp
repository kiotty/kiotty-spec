#include "implementation/context/kiotty_opened_context.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_text.h"
#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

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
