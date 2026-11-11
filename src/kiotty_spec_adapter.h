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

TextResult parseText(KiottySpecText text, size_t max_length);
TextResult parseId(impl::Context& context, KiottySpecText id);
TextResult parseTitle(impl::Context& context, KiottySpecText title);
TextResult parseSummary(impl::Context& context, KiottySpecText summary);
TextResult parseBody(impl::Context& context, KiottySpecText body);
TextResult parseDecision(impl::Context& context, KiottySpecText decision);
TextResult parseDescription(impl::Context& context, KiottySpecText description);
TextResult parsePath(impl::Context& context, KiottySpecText path);
TextResult parseProjectName(impl::Context& context, KiottySpecText project_name);
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
KiottySpecCursorResult cursorResult(impl::Context& context, const impl::CursorIdResult& opened);
KiottySpecNameResult nameResult(std::string_view name);
bool isKind(uint32_t kind);
impl::Result<entity::ItemHandle, KiottySpecStatus> resolve(impl::Context& context,
                                                           KiottySpecText id);
void copyTerminated(impl::Span<char> target, impl::Span<const char> source);

}
