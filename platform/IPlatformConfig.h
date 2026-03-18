#pragma once

#include <string>

namespace main52::platform {

class IPlatformConfig {
public:
    virtual ~IPlatformConfig() = default;
    virtual bool getBool(const char* key, bool fallback) const = 0;
    virtual int getInt(const char* key, int fallback) const = 0;
    virtual std::string getString(const char* key, const char* fallback) const = 0;
    virtual void setBool(const char* key, bool value) = 0;
    virtual void setInt(const char* key, int value) = 0;
    virtual void setString(const char* key, const std::string& value) = 0;
};

} // namespace main52::platform
