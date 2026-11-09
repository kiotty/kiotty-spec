#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_row.h"

#include <cstddef>
#include <cstdint>

struct sqlite3_stmt;

namespace kiotty::spec::implementation {

class DBConnection;

// 준비된 SQL 문장 하나. 생성자에서 prepare 하고, 결과는 status() 로 확인한다.
class Statement
{
public:
    Statement() = default;
    Statement(DBConnection& connection, Span<const char> sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    // prepare 와 bind 에서 처음 난 실패. 실패가 있으면 step · execute 도 그 실패를 돌려준다.
    KiottySpecStatus status() const { return _status; }
    KiottySpecStatus bind(int index, int64_t value);
    KiottySpecStatus bind(int index, double value);
    KiottySpecStatus bind(int index, Span<const char> value);
    KiottySpecStatus bind(int index, std::nullptr_t);
    // 다음 행이 있으면 true
    Result<bool, KiottySpecStatus> step();
    // 끝까지 실행하고 결과 행은 버린다.
    KiottySpecStatus execute();
    Row row() const { return Row(_statement); }
    // 이 연결에서 마지막으로 넣은 행의 rowid · 마지막 문장이 바꾼 행 수
    int64_t insertedRowId() const;
    int64_t changedRowCount() const;

private:
    KiottySpecStatus keep(KiottySpecStatus status);

    sqlite3_stmt* _statement {nullptr};
    KiottySpecStatus _status {KIOTTY_SPEC_STATUS_PRECONDITION};
};

}
