#pragma once

#include "kiotty_spec.h"

#include <string>

namespace kiotty::spec::implementation {

std::string bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category);

}
