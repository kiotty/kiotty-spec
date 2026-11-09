#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 항목 · 본문 · 태그 · 결정 기록 쓰기. 관계는 RelationEditor 가 맡는다. 입력을 검사하고, 체크포인트에 묶고, DB 와 풀을 함께 고친다.
class Editor
{
public:
    Editor(Workspace& workspace, Lifecycle& lifecycle) : _w(workspace), _lifecycle(lifecycle) {}

    Result<entity::IdText, KiottySpecStatus> createItem(entity::ItemKind kind, Span<const char> title, Span<const char> summary);
    // title · summary 가 nullptr 를 가리키는 Span 이면 바꾸지 않는다.
    KiottySpecStatus updateItem(Span<const char> id, Span<const char> title, Span<const char> summary);
    KiottySpecStatus setParent(Span<const char> id, Span<const char> parent_id);
    KiottySpecStatus removeItem(Span<const char> id);
    KiottySpecStatus writeBody(Span<const char> id, Span<const char> body);
    KiottySpecStatus setTag(entity::ItemHandle item, uint32_t tag, uint64_t value);
    KiottySpecStatus writeDecision(entity::ItemKind kind, Span<const char> text);

private:
    Result<entity::IdText, KiottySpecStatus> insertItem(entity::ItemKind kind, Span<const char> title, Span<const char> summary, uint32_t sequence);
    KiottySpecStatus checkText(Span<const char> text, size_t limit, const char* field);
    // 구현 완료인 설계 사항 · 유닛 테스트가 바뀌면 같은 쓰기 안에서 "변경 필요" 로 바꾼다 (3.5).
    KiottySpecStatus markNeedsChange(entity::ItemHandle item, uint32_t sequence);

    Workspace& _w;
    Lifecycle& _lifecycle;
};

}
