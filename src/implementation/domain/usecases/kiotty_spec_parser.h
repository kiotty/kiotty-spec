#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/entity/kiotty_kind.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// .kisp 한 줄을 store 의 버전 표에 넣는다. 태그 · 관계 · Kind 는 이름으로 적혀 있어 모델 표에서 index 를 찾는다.
// SpecFile 의 세부 클래스 (개발수칙 4.19). 실패하면 이유를 남긴다.
class SpecParser
{
public:
    explicit SpecParser(const SpecTables& tables) : _t(tables) {}

    // TABLE 머리줄은 건너뛴다.
    KiottySpecStatus parse(const KispFields& fields);

private:
    KiottySpecStatus parseMeta(const KispFields& fields);
    KiottySpecStatus parseCheckpoint(const KispFields& fields);
    KiottySpecStatus parseItem(const KispFields& fields);
    KiottySpecStatus parseItemVersion(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseTag(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseRelation(const KispFields& fields, uint32_t removed);
    KiottySpecStatus parseDecision(const KispFields& fields);
    Result<entity::ItemKind, KiottySpecStatus> kindOfKey(uint32_t key);
    Result<uint64_t, KiottySpecStatus> parseTagValue(entity::ItemKind kind, uint32_t tag, Span<const char> names) const;

    SpecTables _t;
    bool _versions_sorted {false};
};

}
