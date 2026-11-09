#include "implementation/data/datasource/kiotty_storage.h"

#include "implementation/core/kiotty_text_view.h"

#include <utility>

namespace kiotty::spec::implementation {

Result<Statement, KiottySpecStatus> Storage::prepare(Span<const char> sql)
{
    Statement statement(_connection, sql);
    if (statement.status() != KIOTTY_SPEC_STATUS_SUCCESS) {
        const KiottySpecStatus status = statement.status();
        _failure.raise(status, {literalText("prepare failed ["), sql, literalText("]: "), _connection.message()});
        return Result<Statement, KiottySpecStatus>::failure(status);
    }
    return Result<Statement, KiottySpecStatus>::success(std::move(statement));
}

KiottySpecStatus Storage::executeScript(Span<const char> sql)
{
    const KiottySpecStatus status = _connection.executeScript(sql);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    return _failure.raise(status, {literalText("exec failed: "), _connection.message()});
}

KiottySpecStatus Storage::check(KiottySpecStatus status)
{
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    return _failure.raise(status, {literalText("step: "), _connection.message()});
}

KiottySpecStatus Storage::reject(KiottySpecStatus status, std::initializer_list<Span<const char>> parts)
{
    return _failure.raise(status, parts);
}

}
