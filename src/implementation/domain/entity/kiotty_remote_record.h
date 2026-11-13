#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation::entity {

constexpr size_t MAX_URL_LENGTH = KIOTTY_SPEC_MAX_URL_LENGTH;
constexpr size_t MAX_COMMIT_LENGTH = KIOTTY_SPEC_MAX_COMMIT_LENGTH;

using UrlText = FixedString<MAX_URL_LENGTH>;
using CommitText = FixedString<MAX_COMMIT_LENGTH>;

struct RemoteRecord {
    UrlText url {};
    CommitText rule_base {};
};

static_assert(sizeof(RemoteRecord) == 577, "unexpected padding");

bool isValidCommit(Span<const char> commit);
KiottySpecStatus checkRemote(const RemoteRecord& remote);

}
