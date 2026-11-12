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
using ConfigurationResult = impl::Result<impl::Configuration, KiottySpecStatus>;

using HandleResult = impl::Result<entity::ItemHandle, KiottySpecStatus>;
using RecordResult = impl::Result<const entity::ItemRecord*, KiottySpecStatus>;

enum class Field : uint8_t { ID, TITLE, SUMMARY, BODY, DECISION, DESCRIPTION, PATH, PROJECT_NAME };

class Boundary
{
public:
    explicit Boundary(impl::Context& context) : _context(context) {}

    TextResult parse(KiottySpecText text, Field field) const;
    HandleResult resolve(KiottySpecText id) const;
    RecordResult read(KiottySpecItemHandle item) const;
    KiottySpecCursorResult wrap(const impl::CursorIdResult& opened) const;

private:
    impl::Context& _context;
};

TextResult parseText(KiottySpecText text, size_t max_length);
ConfigurationResult parseConfiguration(const KiottySpecConfiguration& configuration);

KiottySpecItemHandle toHandle(entity::ItemHandle handle);
entity::ItemHandle fromHandle(KiottySpecItemHandle handle);
KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle);
KiottySpecItem toItem(const entity::ItemRecord& record);
KiottySpecCheckpoint toCheckpointRecord(const entity::CheckpointRecord& record);
KiottySpecTag toTag(KiottySpecItemHandle item, KiottySpecItemKind model, uint32_t tag,
                    uint64_t value);
KiottySpecStatusResult status(KiottySpecStatus code);
KiottySpecSizeResult sizeResult(KiottySpecStatus code, size_t value);
KiottySpecCursorResult cursorFailure(KiottySpecStatus code);
KiottySpecNameResult nameResult(std::string_view name);
bool isKind(uint32_t kind);
void copyTerminated(impl::Span<char> target, impl::Span<const char> source);

}
