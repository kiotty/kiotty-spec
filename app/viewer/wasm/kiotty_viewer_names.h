#pragma once

#include "kiotty_spec.h"

#include <string>
#include <string_view>

namespace kiotty::viewer {

bool isSuccess(KiottySpecStatus status);
KiottySpecText makeText(std::string_view text);
std::string formatSubject(KiottySpecSubject subject);
std::string formatCondition(KiottySpecCondition condition);
std::string formatKind(KiottySpecItemKind kind);
std::string formatState(KiottySpecCheckpointState state);
std::string formatCheckpoint(KiottySpecCheckpointHandle handle);

}
