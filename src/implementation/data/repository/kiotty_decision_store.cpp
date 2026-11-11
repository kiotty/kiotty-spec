#include "implementation/data/repository/kiotty_decision_store.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

KiottySpecStatus DecisionStore::write(entity::ItemKind kind, Span<const char> text,
                                      uint32_t sequence)
{
    const auto slot = _versions.slot(static_cast<uint32_t>(kind), sequence);
    if (!slot) {
        return _failure.raise(slot.code, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_decision_versions"));
    }
    slot.value->kind = kind;
    slot.value->removed = 0;
    return slot.value->text.assign(text);
}

Result<size_t, KiottySpecStatus> DecisionStore::read(entity::ItemKind kind, uint32_t checkpoint,
                                                     char* buffer, size_t capacity) const
{
    using SizeResult = Result<size_t, KiottySpecStatus>;
    const entity::DecisionVersion* version =
        _versions.latest(static_cast<uint32_t>(kind), checkpoint);
    const Span<const char> text = version == nullptr ? Span<const char>() : version->text.view();
    const size_t length = text.length();
    if (buffer == nullptr || capacity <= length) {
        return SizeResult { KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, length + 1 };
    }
    for (size_t index = 0; index < length; ++index) { buffer[index] = text[index]; }
    buffer[length] = '\0';
    return SizeResult::success(length);
}

}
