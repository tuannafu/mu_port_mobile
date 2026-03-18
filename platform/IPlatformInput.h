#pragma once

#include "PlatformTypes.h"

namespace main52::platform {

class IPlatformInput {
public:
    virtual ~IPlatformInput() = default;
    virtual void update() = 0;
    virtual MouseState mouse() const = 0;
    virtual bool isKeyDown(int keyCode) const = 0;
    virtual bool wasKeyPressed(int keyCode) const = 0;
};

} // namespace main52::platform
