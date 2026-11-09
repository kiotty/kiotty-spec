#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/usecases/kiotty_model_finder.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

// 정의서 v0 → 설계서 v1 대응 (설계서 v1 11절). LegacyImporter 가 쓴다.

using LegacyTags = FixedString<64>;

// 단계 번호. purpose 가 "release" 인 10 단계는 12 로 간다.
int64_t mapLegacyStep(int64_t step, std::string_view purpose);
// 옛 Kind 이름 → 새 Kind 이름. 없으면 빈 글
std::string_view mapLegacyKind(Span<const char> kind);
// category → "[[tag, bit]]". 대응이 없으면 "[]"
LegacyTags legacyTags(const ModelFinder& finder, entity::ItemKind kind, Span<const char> category);
// 옛 링크 종류 → 관계 이름. 없으면 nullptr
const char* legacyRelation(std::string_view from_kind, Span<const char> type, std::string_view to_kind);

}
