#include "implementation/data/repository/kiotty_meta_store.h"

namespace kiotty::spec::implementation {

KiottySpecStatus MetaStore::set(Span<const char> key, Span<const char> value)
{
    MetaKey name;
    MetaValue content;
    KiottySpecStatus status = name.assign(key);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = content.assign(value); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    _entries.erase(name);
    return _entries.insert(name, content);
}

Result<MetaValue, KiottySpecStatus> MetaStore::get(Span<const char> key) const
{
    MetaKey name;
    if (name.assign(key) != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<MetaValue, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return _entries.find(name);
}

}
