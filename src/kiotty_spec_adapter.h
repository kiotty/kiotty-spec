#pragma once

// C ↔ implementation 어댑터가 함께 쓰는 도우미. 입력을 Span 으로, 내부 레코드를 C 구조체로 바꾼다.

#include "kiotty_spec.h"
#include "implementation/context/kiotty_context.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <cstddef>
#include <cstdint>

struct KiottySpecContext {
    explicit KiottySpecContext(const kiotty::spec::implementation::Configuration& configuration) : context(configuration) {}
    kiotty::spec::implementation::Context context;
};

namespace kiotty::spec::adapter {

namespace impl = kiotty::spec::implementation;
namespace entity = kiotty::spec::implementation::entity;

constexpr uint32_t SHIFT = 32;

impl::Span<const char> spanOf(KiottySpecText text);
KiottySpecItemHandle toHandle(entity::ItemHandle handle);
entity::ItemHandle fromHandle(KiottySpecItemHandle handle);
KiottySpecCheckpointHandle toCheckpoint(entity::CheckpointHandle handle);
KiottySpecItem toItem(const entity::ItemRecord& record);
KiottySpecCheckpoint toCheckpointRecord(const entity::CheckpointRecord& record);
KiottySpecStatusResult status(KiottySpecStatus code);
KiottySpecCursorResult cursorResult(impl::Context& context, const impl::CursorIdResult& opened);
bool isKind(uint32_t kind);
impl::Result<entity::ItemHandle, KiottySpecStatus> resolve(impl::Context& context, KiottySpecText id);
KiottySpecSizeResult copyOut(impl::Span<const char> text, char* buffer, size_t capacity);

}
