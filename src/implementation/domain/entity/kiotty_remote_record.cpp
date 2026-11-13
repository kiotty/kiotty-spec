#include "kiotty_remote_record.h"

#include "implementation/core/kiotty_utf8.h"

namespace kiotty::spec::implementation::entity {

namespace {

bool isLowerHexDigit(char letter);

}

KiottySpecStatus checkRemote(const RemoteRecord& remote)
{
    const bool is_utf8 = isValidUtf8(remote.url.view());
    if (!is_utf8) { return KIOTTY_SPEC_STATUS_INVALID_UTF8; }
    const bool is_commit = isValidCommit(remote.rule_base.view());
    if (!is_commit) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    const bool is_orphan_base = remote.url.empty() && !remote.rule_base.empty();
    return is_orphan_base ? KIOTTY_SPEC_STATUS_INVALID_ARGUMENT : KIOTTY_SPEC_STATUS_SUCCESS;
}

bool isValidCommit(Span<const char> commit)
{
    for (const char letter : commit) {
        const bool is_digit = isLowerHexDigit(letter);
        if (!is_digit) { return false; }
    }
    return true;
}

namespace {

bool isLowerHexDigit(char letter)
{
    const bool is_number = letter >= '0' && letter <= '9';
    const bool is_letter = letter >= 'a' && letter <= 'f';
    return is_number || is_letter;
}

}

}
