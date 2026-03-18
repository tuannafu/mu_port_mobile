#pragma once

#include <cstdint>
#include <string>

namespace main52::platform {

struct Size {
    int width = 0;
    int height = 0;
};

struct Point {
    int x = 0;
    int y = 0;
};

struct MouseState {
    Point position{};
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
    int wheelDelta = 0;
};

struct FrameTiming {
    double deltaSeconds = 0.0;
    double absoluteSeconds = 0.0;
};

} // namespace main52::platform
