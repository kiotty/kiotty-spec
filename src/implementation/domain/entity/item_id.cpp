#include "item_id.h"
#include "item_records.h"

namespace kiotty::spec::implementation::entity {

namespace {

bool isAlphanumeric(char letter)
{
    return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9');
}

}

KiottySpecStatus validateItemId(Span<const char> id)
{
    if (id.length() == 0) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    if (id.length() > MAX_ID_LENGTH) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    if (!isAlphanumeric(id[0])) { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    for (const char letter : id) {
        if (!isAlphanumeric(letter) && letter != '-' && letter != '_') { return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
