#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_context.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

struct KiottySpecContext {
    explicit KiottySpecContext(const kiotty::spec::implementation::Configuration& configuration)
        : context(configuration)
    {}
    kiotty::spec::implementation::Context context;
};

namespace kiotty::spec::adapter {

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;

constexpr uint32_t SHIFT = 32;
constexpr uint64_t LOW_MASK = 0xFFFFFFFFu;

using TextResult = impl::Result<impl::Span<const char>, KiottySpecStatus>;
using OpenedResult = impl::OpenedContextResult;
using ConfigurationResult = impl::Result<impl::Configuration, KiottySpecStatus>;

using HandleResult = impl::Result<entity::ItemHandle, KiottySpecStatus>;
using RecordResult = impl::Result<const entity::ItemState&, KiottySpecStatus>;
using LabelResult = impl::Result<const entity::ItemLabel&, KiottySpecStatus>;
using KindResult = impl::Result<entity::ItemKind, KiottySpecStatus>;
using StepResult = impl::Result<entity::Step, KiottySpecStatus>;
using ActorResult = impl::Result<entity::Actor, KiottySpecStatus>;
using SequenceResult = impl::Result<uint32_t, KiottySpecStatus>;
using TagResult = impl::Result<entity::TagRecord, KiottySpecStatus>;

enum class Field : uint8_t { ID, TITLE, SUMMARY, BODY, DECISION, DESCRIPTION, PATH, PROJECT_NAME };

class FieldParser
{
public:
    explicit FieldParser(entity::Failure& failure) : _failure(failure) {}

    TextResult parse(KiottySpecText text, Field field) const;

private:
    entity::Failure& _failure;
};

class Boundary
{
public:
    explicit Boundary(impl::OpenedContext& context) : _context(context) {}

    TextResult parse(KiottySpecText text, Field field) const;
    TextResult parseNewId(KiottySpecText id) const;
    HandleResult resolve(KiottySpecText id) const;
    RecordResult read(KiottySpecItemHandle item) const;
    KiottySpecCursorResult wrap(const impl::CursorIdResult& opened) const;
    KiottySpecStatus checkTag(entity::ItemKind kind, uint32_t tag,
                              impl::Span<const char> id) const;

private:
    impl::OpenedContext& _context;
};

class ValueParser
{
public:
    explicit ValueParser(impl::OpenedContext& context) : _context(context) {}

    KindResult parseKind(KiottySpecItemKind kind) const;
    KindResult parseDecisionKind(KiottySpecItemKind kind) const;
    StepResult parseStep(KiottySpecStep step) const;
    ActorResult parseActor(KiottySpecActor actor) const;
    SequenceResult parseCheckpoint(KiottySpecCheckpointHandle handle) const;
    TagResult parseTag(KiottySpecItemHandle item, uint32_t tag, uint64_t value) const;

private:
    KiottySpecStatus raise(KiottySpecStatus code, KiottySpecSubject subject) const;

    impl::OpenedContext& _context;
};

OpenedResult findOpenedContext(KiottySpecContext* context);
TextResult parseText(KiottySpecText text, size_t max_length);
ConfigurationResult parseConfiguration(const KiottySpecConfiguration& configuration);

KiottySpecItemHandle toHandle(entity::ItemHandle handle);
entity::ItemHandle fromHandle(KiottySpecItemHandle handle);
KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle);
KiottySpecItem toItem(const entity::ItemState& state, const entity::ItemLabel& label);
KiottySpecCheckpoint toCheckpointRecord(const entity::CheckpointRecord& record,
                                        const entity::DescriptionText& description);
KiottySpecTag toTag(KiottySpecItemHandle item, KiottySpecItemKind model, uint32_t tag,
                    uint64_t value);
KiottySpecStatusResult status(KiottySpecStatus code);
KiottySpecSizeResult sizeResult(KiottySpecStatus code, size_t value);
KiottySpecCursorResult cursorFailure(KiottySpecStatus code);
KiottySpecNameResult nameResult(std::string_view name);
bool isKind(uint32_t kind);
void copyTerminated(impl::Span<char> target, impl::Span<const char> source);
impl::Span<char> toSpan(KiottySpecBuffer buffer);
impl::OptionalText toOptionalText(impl::Span<const char> text);

}
