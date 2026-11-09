#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"

struct sqlite3;

namespace kiotty::spec::implementation {

// SQLite DB 하나와의 연결. 문장은 Statement 가 이 연결로 준비한다.
class DBConnection
{
public:
    DBConnection() = default;
    ~DBConnection();

    DBConnection(const DBConnection&) = delete;
    DBConnection& operator=(const DBConnection&) = delete;

    KiottySpecStatus open(Span<const char> path);
    void close();
    bool isOpen() const { return _database != nullptr; }
    // ';' 로 이은 여러 문장을 차례로 실행한다. 결과 행은 버린다.
    KiottySpecStatus executeScript(Span<const char> sql);
    Span<const char> message() const;

private:
    friend class Statement;

    sqlite3* _database {nullptr};
    int _open_code {0};
};

}
