#pragma once

#include <string>
#include <string_view>

namespace ircp {

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static void setLevel(Level level);
    static void setLevel(std::string_view name);
    static void debug(std::string_view message);
    static void info(std::string_view message);
    static void warn(std::string_view message);
    static void error(std::string_view message);

private:
    static void write(Level level, std::string_view message);
};

}  // namespace ircp
