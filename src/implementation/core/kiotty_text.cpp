#include "implementation/core/kiotty_text.h"

#include <chrono>

namespace kiotty::spec::implementation {

int64_t nowEpochSeconds()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}
