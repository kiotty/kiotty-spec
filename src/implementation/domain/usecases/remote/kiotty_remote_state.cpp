#include "implementation/domain/usecases/remote/kiotty_remote_state.h"

#include "implementation/core/kiotty_text_view.h"

#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::RemoteRecord;

constexpr std::string_view URL_KEY = "remote.url";
constexpr std::string_view RULE_BASE_KEY = "remote.rule_base";

KiottySpecStatus storeValue(MetaStore& meta, std::string_view key, Span<const char> value);

}

Result<RemoteRecord, KiottySpecStatus> RemoteState::read() const
{
    using RemoteResult = Result<RemoteRecord, KiottySpecStatus>;
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return RemoteResult::failure(fresh); }
    RemoteRecord remote {};
    const auto url = _workspace.meta.get(spanOf(URL_KEY));
    const auto base = _workspace.meta.get(spanOf(RULE_BASE_KEY));
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    if (url) { status = remote.url.assign(url.value.view()); }
    if (base && status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = remote.rule_base.assign(base.value.view());
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return RemoteResult::failure(status); }
    return RemoteResult::success(remote);
}

KiottySpecStatus RemoteState::write(const RemoteRecord& remote)
{
    const KiottySpecStatus checked = entity::checkRemote(remote);
    if (checked != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _workspace.failure.raise(checked, KIOTTY_SPEC_SUBJECT_REMOTE);
    }
    const KiottySpecStatus fresh = _workspace.refresh();
    if (fresh != KIOTTY_SPEC_STATUS_SUCCESS) { return fresh; }
    KiottySpecStatus status = storeValue(_workspace.meta, URL_KEY, remote.url.view());
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        status = storeValue(_workspace.meta, RULE_BASE_KEY, remote.rule_base.view());
    }
    return _workspace.finish(status);
}

KiottySpecStatus RemoteState::writeBase(Span<const char> rule_base)
{
    const bool is_commit = entity::isValidCommit(rule_base);
    if (!is_commit) {
        return _workspace.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                                        KIOTTY_SPEC_SUBJECT_REMOTE);
    }
    return storeValue(_workspace.meta, RULE_BASE_KEY, rule_base);
}

namespace {

KiottySpecStatus storeValue(MetaStore& meta, std::string_view key, Span<const char> value)
{
    const bool is_empty = value.length() == 0;
    if (is_empty) {
        meta.erase(spanOf(key));
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    return meta.set(spanOf(key), value);
}

}

}
