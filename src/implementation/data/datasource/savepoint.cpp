#include "implementation/data/datasource/savepoint.h"

#include "implementation/core/text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char BEGIN_SQL[] = "SAVEPOINT kiotty_sp";
constexpr char COMMIT_SQL[] = "RELEASE kiotty_sp";
constexpr char ROLLBACK_SQL[] = "ROLLBACK TO kiotty_sp; RELEASE kiotty_sp;";

}

Savepoint::~Savepoint()
{
    if (_is_open) { _connection.execute(literalText(ROLLBACK_SQL)); }
}

KiottySpecStatus Savepoint::begin()
{
    const KiottySpecStatus status = _connection.execute(literalText(BEGIN_SQL));
    _is_open = status == KIOTTY_SPEC_STATUS_SUCCESS;
    return status;
}

KiottySpecStatus Savepoint::commit()
{
    const KiottySpecStatus status = _connection.execute(literalText(COMMIT_SQL));
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { _is_open = false; }
    return status;
}

}
