#pragma once

#include "implementation/base/context_builder.h"
#include "implementation/base/meta.h"
#include "implementation/core/records.h"

#include <string>
#include <vector>

namespace kiotty::base {

// 재개 지원: 지금 어느 체크포인트·단계에 있는지, 어떤 단계를 밟아왔는지, 다음에 무엇을 할지.
class Journey {
public:
    Journey(Stores stores, Meta &meta) : s_(stores), meta_(meta) {}

    core::PositionRecord position();
    std::vector<core::StepRunRecord> runs();
    std::string whereAmI(size_t maxBytes);

private:
    Stores s_;
    Meta &meta_;
};

} // namespace kiotty::base
