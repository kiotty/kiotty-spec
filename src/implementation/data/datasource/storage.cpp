#include "implementation/data/datasource/storage.h"

#include "implementation/core/text_view.h"

namespace kiotty::spec::implementation {

Result<Query, KiottySpecStatus> Storage::prepare(Span<const char> sql)
{
    auto prepared = _connection.prepare(sql);
    if (!prepared) {
        _failure.raise(prepared.code, {literalText("prepare failed ["), sql, literalText("]: "), _connection.message()});
    }
    return prepared;
}

KiottySpecStatus Storage::execute(Span<const char> sql)
{
    const KiottySpecStatus status = _connection.execute(sql);
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
