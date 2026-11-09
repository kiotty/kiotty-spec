#include "implementation/data/datasource/kiotty_savepoint.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char BEGIN_SQL[] = "SAVEPOINT kiotty_sp";
constexpr char COMMIT_SQL[] = "RELEASE kiotty_sp";
constexpr char ROLLBACK_SQL[] = "ROLLBACK TO kiotty_sp; RELEASE kiotty_sp;";

}

Savepoint::~Savepoint() { rollback(); }

KiottySpecStatus Savepoint::begin()
{
    const KiottySpecStatus status = _connection.executeScript(literalText(BEGIN_SQL));
    _is_open = status == KIOTTY_SPEC_STATUS_SUCCESS;
    return status;
}

KiottySpecStatus Savepoint::commit()
{
    const KiottySpecStatus status = _connection.executeScript(literalText(COMMIT_SQL));
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { _is_open = false; }
    return status;
}


KiottySpecStatus Savepoint::rollback()
{
    if (!_is_open) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    _is_open = false;
    return _connection.executeScript(literalText(ROLLBACK_SQL));
}

}
