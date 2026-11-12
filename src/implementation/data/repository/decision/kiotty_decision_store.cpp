#include "implementation/data/repository/decision/kiotty_decision_store.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/repository/item/kiotty_body_store.h"

namespace kiotty::spec::implementation {

namespace {

Handle pickTextHandle(const entity::DecisionVersion& version) { return version.text; }

}

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
    const auto handle = reserveText(version.text);
    if (!handle) {
        return _failure.raise(handle.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_decision_versions"));
    }
    version.text = handle.value;
    const auto stored = _texts.get(handle.value);
    if (!stored) { return stored.code; }
    return (*stored.value).assign(text);
}

Result<size_t, KiottySpecStatus> DecisionStore::read(entity::ItemKind kind, uint32_t checkpoint,
                                                     Span<char> buffer) const
{
    const DecisionVersionResult version =
        _versions.latest(static_cast<uint32_t>(kind), checkpoint);
    if (!version) { return copyBody(Span<const char>(), buffer); }
    const entity::DecisionVersion& latest = *version.value;
    return copyBody(_texts.read(latest.text).view(), buffer);
}

Result<entity::DecisionTextHandle, KiottySpecStatus>
DecisionStore::reserveText(entity::DecisionTextHandle current)
{
    using HandleResult = Result<entity::DecisionTextHandle, KiottySpecStatus>;
    const bool has_text = current.isValid();
    if (has_text) { return HandleResult::success(current); }
    return _texts.acquire(_versions.all(), pickTextHandle);
}

}
