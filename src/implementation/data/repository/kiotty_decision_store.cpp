#include "implementation/data/repository/kiotty_decision_store.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/kiotty_body_store.h"

namespace kiotty::spec::implementation {

KiottySpecStatus DecisionStore::write(entity::ItemKind kind, Span<const char> text,
                                      uint32_t sequence)
{
    const auto slot = _versions.slot(static_cast<uint32_t>(kind), sequence);
    if (!slot) {
        return _failure.raise(slot.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_decision_versions"));
    }
    entity::DecisionVersion& version = *slot.value;
    version.kind = kind;
    version.removed = 0;
    return version.text.assign(text);
}

Result<size_t, KiottySpecStatus> DecisionStore::read(entity::ItemKind kind, uint32_t checkpoint,
                                                     Span<char> buffer) const
{
    const DecisionVersionResult version =
        _versions.latest(static_cast<uint32_t>(kind), checkpoint);
    if (!version) { return copyBody(Span<const char>(), buffer); }
    const entity::DecisionVersion& latest = *version.value;
    return copyBody(latest.text.view(), buffer);
}

}
