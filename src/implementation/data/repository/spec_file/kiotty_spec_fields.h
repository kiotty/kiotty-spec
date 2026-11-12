#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/entity/kiotty_kind.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

using FieldResult = Result<Span<const char>, KiottySpecStatus>;
using KeyResult = Result<uint32_t, KiottySpecStatus>;
using NumberResult = Result<uint64_t, KiottySpecStatus>;

KeyResult parseOptionalKey(const KispFields& fields, size_t index);
KeyResult parseKey(const KispFields& fields, size_t index);
KeyResult parseSequence(const KispFields& fields, size_t index);
NumberResult parseNumber(const KispFields& fields, size_t index, uint64_t maximum);
NumberResult parseHash(const KispFields& fields, size_t index);
Result<entity::ItemKind, KiottySpecStatus> parseKindName(const KispFields& fields, size_t index);
FieldResult readText(const KispFields& fields, size_t index, size_t limit);
std::string_view viewField(const KispFields& fields, size_t index);
FieldResult readField(const KispFields& fields, size_t index);

}
