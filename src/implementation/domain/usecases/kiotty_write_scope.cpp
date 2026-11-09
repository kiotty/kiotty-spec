#include "implementation/domain/usecases/kiotty_write_scope.h"

namespace kiotty::spec::implementation {

WriteScope::WriteScope(Workspace& workspace, Lifecycle& lifecycle)
    : _w(workspace), _savepoint(workspace.storage.connection()), _sequence(Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_PRECONDITION))
{
    const KiottySpecStatus begun = _w.storage.check(_savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) {
        _sequence = Result<uint32_t, KiottySpecStatus>::failure(begun);
        return;
    }
    _touched = true;
    _sequence = lifecycle.beginWrite();
}

KiottySpecStatus WriteScope::finish(KiottySpecStatus status)
{
    KiottySpecStatus result = _sequence ? status : _sequence.code;
    if (result == KIOTTY_SPEC_STATUS_SUCCESS) { result = _w.storage.check(_savepoint.commit()); }
    if (result == KIOTTY_SPEC_STATUS_SUCCESS) { return result; }
    _savepoint.rollback();
    if (_touched) { _w.reload(); }
    return result;
}

}
