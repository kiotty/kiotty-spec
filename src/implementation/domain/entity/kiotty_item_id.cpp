#include "kiotty_item_id.h"
#include "kiotty_item_records.h"

namespace kiotty::spec::implementation::entity {

namespace {

bool isAlphanumeric(char letter);

}

KiottySpecStatus validateItemId(Span<const char> id)
{
    const size_t length = id.length();
    if (length == 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (length > MAX_ID_LENGTH) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    for (size_t index = 0; index < length; ++index) {
        const char letter = id[index];
        const bool is_alphanumeric = isAlphanumeric(letter);
        const bool is_separator = index > 0 && (letter == '-' || letter == '_');
        const bool is_allowed = is_alphanumeric || is_separator;
        if (!is_allowed) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

bool isAlphanumeric(char letter)
{
    return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
           (letter >= '0' && letter <= '9');
}

}

}
