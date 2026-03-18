#pragma once

#include "PlatformTypes.h"

namespace main52::platform {

class IPlatformTimer {
public:
    virtual ~IPlatformTimer() = default;
    virtual void reset() = 0;
    virtual FrameTiming tick() = 0;
};

} // namespace main52::platform
