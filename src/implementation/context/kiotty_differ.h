#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_cursor_pool.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/context/kiotty_item_diff_query.h"
#include "implementation/context/kiotty_past_reader.h"
#include "implementation/context/kiotty_relation_diff_query.h"
#include "implementation/context/kiotty_text_differ.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct KindCounts {
    uint32_t added {0};
    uint32_t modified {0};
    uint32_t removed {0};
    uint32_t relations_added {0};
    uint32_t relations_removed {0};
    uint32_t decision_changed {0};
};

// 체크포인트 사이 비교 (설계서 v1 5.4). 버전 행만 읽고 본문 내용은 돌려주지 않는다.
class Differ
{
public:
    Differ(Workspace& workspace, CursorPool& cursors, TextBuffer& work) : _w(workspace), _cursors(cursors), _work(work) {}

    Result<DiffRange, KiottySpecStatus> range(uint64_t from, uint64_t to);
    // 0 이면 가장 최근. 없는(정리된) 체크포인트면 NOT_FOUND
    Result<uint32_t, KiottySpecStatus> point(uint64_t checkpoint);

    KiottySpecStatus summarize(DiffRange range, uint32_t kind_mask, uint32_t field_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    CursorIdResult items(DiffRange range, uint32_t kind_mask, uint32_t field_mask);
    CursorIdResult relations(DiffRange range, uint32_t kind_mask);

    // 그 시점 읽기 · 글자 diff 는 세부 클래스에 맡긴다 (개발수칙 4.19). 부른 자리에서 쓰고 버린다.
    PastReader past() { return PastReader(_w); }
    TextDiffer text() { return TextDiffer(_w, _cursors, _work); }

private:
    KiottySpecStatus countItems(DiffRange range, uint32_t kind_mask, uint32_t field_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countRelations(DiffRange range, uint32_t kind_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus countDecisions(DiffRange range, uint32_t kind_mask, KindCounts (&counts)[entity::ITEM_KIND_COUNT]);
    KiottySpecStatus pushRow(CursorId cursor, const CursorRow& row, Span<const char> overflow);

    Workspace& _w;
    CursorPool& _cursors;
    TextBuffer& _work;
};

// 커서 행에 담은 항목 diff · 관계 diff 를 푼다.
ItemDiffRow unpackItemDiff(const CursorRow& row);
RelationDiffRow unpackRelationDiff(const CursorRow& row);

}
