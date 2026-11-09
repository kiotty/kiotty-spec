#pragma once

#include "kiotty_spec.h"
#include "implementation/context/cursor_pool.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/core/text_buffer.h"
#include "implementation/domain/entity/item_records.h"
#include "implementation/domain/usecases/workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct DiffRange {
    uint32_t from {0};
    uint32_t to {0};
};

struct KindCounts {
    uint32_t added {0};
    uint32_t modified {0};
    uint32_t removed {0};
    uint32_t relations_added {0};
    uint32_t relations_removed {0};
    uint32_t decision_changed {0};
};

// 그 시점의 항목 버전 (설계서 v1 5.4 readItemAt)
struct ItemVersion {
    entity::ItemRecord record {};
    entity::IdText parent_id;
};

struct TextPiece {
    const char* text {nullptr};
    size_t length {0};
    uint32_t change {0};
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

    Result<ItemVersion, KiottySpecStatus> itemAt(Span<const char> id, uint32_t checkpoint);
    Result<uint64_t, KiottySpecStatus> tagAt(Span<const char> id, uint32_t tag, uint32_t checkpoint);
    Result<size_t, KiottySpecStatus> bodyAt(Span<const char> id, uint32_t checkpoint, char* buffer, size_t capacity);
    Result<size_t, KiottySpecStatus> decisionAt(entity::ItemKind kind, uint32_t checkpoint, char* buffer, size_t capacity);

    CursorIdResult openText(entity::ItemHandle item, uint32_t field, DiffRange range);
    Result<TextPiece, KiottySpecStatus> nextText(CursorId cursor);

private:
    template <typename Visit>
    KiottySpecStatus eachItemDiff(DiffRange range, uint32_t kind_mask, uint32_t field_mask, Visit visit);
    template <typename Visit>
    KiottySpecStatus eachRelationDiff(DiffRange range, uint32_t kind_mask, Visit visit);

    Workspace& _w;
    CursorPool& _cursors;
    TextBuffer& _work;
};

// 커서 행에 담은 항목 diff · 관계 diff 를 푼다.
struct ItemDiffRow {
    entity::IdText id;
    uint32_t before;
    uint32_t after;
    uint32_t kind;
    uint32_t change;
    uint32_t fields;
};

struct RelationDiffRow {
    entity::IdText from_id;
    entity::IdText to_id;
    uint32_t checkpoint;
    uint32_t relation;
    uint32_t model;
    uint32_t change;
};

ItemDiffRow unpackItemDiff(const CursorRow& row);
RelationDiffRow unpackRelationDiff(const CursorRow& row);

}
