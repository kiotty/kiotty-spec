#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// 그 시점의 항목 버전 (설계서 v1 5.4 readItemAt)
struct ItemVersionView {
    entity::ItemRecord record {};
    entity::IdText parent_id {};
};

// 과거 체크포인트 시점의 항목 · 태그 · 본문 · 결정 기록을 읽는다 (설계서 v1 5.4). Differ 의 세부 클래스 (개발수칙 4.19).
class PastReader
{
public:
    explicit PastReader(Workspace& workspace) : _w(workspace) {}

    Result<ItemVersionView, KiottySpecStatus> itemAt(Span<const char> id, uint32_t checkpoint);
    Result<uint64_t, KiottySpecStatus> tagAt(Span<const char> id, uint32_t tag, uint32_t checkpoint);
    Result<size_t, KiottySpecStatus> bodyAt(Span<const char> id, uint32_t checkpoint, char* buffer, size_t capacity);
    Result<size_t, KiottySpecStatus> decisionAt(entity::ItemKind kind, uint32_t checkpoint, char* buffer, size_t capacity);

private:
    Result<const entity::ItemVersion*, KiottySpecStatus> versionAt(Span<const char> id, uint32_t checkpoint);

    Workspace& _w;
};

}
