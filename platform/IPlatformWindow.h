#pragma once

#include "PlatformTypes.h"

namespace main52::platform {

class IPlatformWindow {
public:
    virtual ~IPlatformWindow() = default;
    virtual bool create(const char* title, Size size, bool windowed) = 0;
    virtual void destroy() = 0;
    virtual void present() = 0;
    virtual Size getSize() const = 0;
    virtual bool isActive() const = 0;
};

} // namespace main52::platform
