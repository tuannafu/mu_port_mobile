#pragma once

namespace main52::platform {

class IPlatformApp {
public:
    virtual ~IPlatformApp() = default;
    virtual bool initialize() = 0;
    virtual bool pumpEvents() = 0;
    virtual void shutdown() = 0;
};

} // namespace main52::platform
