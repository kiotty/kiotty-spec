#pragma once

#include "implementation/context/context_builder.h"
#include "implementation/data/repository/meta.h"
#include "implementation/domain/entity/records.h"

#include <string>
#include <vector>

namespace kiotty::spec::implementation {

class Journey {
public:
    Journey(Stores stores, Meta &meta) : s_(stores), meta_(meta) {}

    PositionRecord position();
    std::vector<StepRunRecord> runs();
    std::string whereAmI(size_t maxBytes);

private:
    Stores s_;
    Meta &meta_;
};

}
