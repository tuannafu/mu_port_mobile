#pragma once

#include "IPlatformApp.h"
#include "IPlatformWindow.h"
#include "IPlatformInput.h"
#include "IPlatformTimer.h"
#include "IPlatformConfig.h"

namespace main52::platform {

struct PlatformServices {
    IPlatformApp* app = nullptr;
    IPlatformWindow* window = nullptr;
    IPlatformInput* input = nullptr;
    IPlatformTimer* timer = nullptr;
    IPlatformConfig* config = nullptr;
};

} // namespace main52::platform
