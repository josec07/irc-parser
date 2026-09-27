#pragma once
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <memory>

class Logger {
public:
    // Initialize the logger (called once in main or during setup)
    static void initialize(const std::string& logger_name, const std::string& log_file_path);

    // Get the logger instance
    static spdlog::logger& get_logger();

    // Convenience log functions
    static void info(const std::string& message);
    static void error(const std::string& message);
    static void warn(const std::string& message);
    static void debug(const std::string& message);

private:

    // shared ptr creates type
    static std::shared_ptr<spdlog::logger> logger_;
};