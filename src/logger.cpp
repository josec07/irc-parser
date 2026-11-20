#include "ircp/logger.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>

namespace ircp {
namespace {

Logger::Level g_level = Logger::Level::Info;
std::mutex g_mu;

const char* levelName(Logger::Level level) {
    switch (level) {
        case Logger::Level::Debug:
            return "DEBUG";
        case Logger::Level::Info:
            return "INFO";
        case Logger::Level::Warn:
            return "WARN";
        case Logger::Level::Error:
            return "ERROR";
    }
    return "INFO";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&seconds, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

void Logger::setLevel(Level level) { g_level = level; }

void Logger::setLevel(std::string_view name) {
    if (name == "debug") {
        g_level = Level::Debug;
    } else if (name == "info") {
        g_level = Level::Info;
    } else if (name == "warn" || name == "warning") {
        g_level = Level::Warn;
    } else if (name == "error") {
        g_level = Level::Error;
    }
}

void Logger::debug(std::string_view message) { write(Level::Debug, message); }
void Logger::info(std::string_view message) { write(Level::Info, message); }
void Logger::warn(std::string_view message) { write(Level::Warn, message); }
void Logger::error(std::string_view message) { write(Level::Error, message); }

void Logger::write(Level level, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(g_level)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    std::cerr << timestamp() << " " << levelName(level) << " " << message << '\n';
}

}  // namespace ircp
