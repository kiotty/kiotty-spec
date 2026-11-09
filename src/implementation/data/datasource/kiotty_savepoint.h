#pragma once

#include "kiotty_spec.h"
#include "implementation/data/datasource/kiotty_db_connection.h"

namespace kiotty::spec::implementation {

class Savepoint
{
public:
    explicit Savepoint(DBConnection& connection) : _connection(connection) {}
    ~Savepoint();

    Savepoint(const Savepoint&) = delete;
    Savepoint& operator=(const Savepoint&) = delete;

    KiottySpecStatus begin();
    KiottySpecStatus commit();
    // 열려 있으면 되돌리고 닫는다.
    KiottySpecStatus rollback();

private:
    DBConnection& _connection;
    bool _is_open {false};
};

}
