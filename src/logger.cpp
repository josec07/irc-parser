#include "ircp/logger.h"
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog-inl.h>

using spdlog::sinks::basic_file_sink_mt;

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::initialize(const std::string &logger_name,
                                const std::string &log_file_path) {
  if (logger_ != nullptr) {
    spdlog::info("Logger already initialized.");
    return;
  }

  // Create a file sink
  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);


  logger_ = std::make_shared<spdlog::logger>(logger_name, file_sink);
  // Set the logger as the default (optional)
  spdlog::set_default_logger(logger_);

  // Set log level (optional)
  logger_->set_level(spdlog::level::info);
}

spdlog::logger &Logger::get_logger() {
  if (logger_ == nullptr) {
    throw std::runtime_error("Logger not initialized.");
  }
  return *logger_;
}

void Logger::info(const std::string &message) {
  get_logger().info(message);
}

void Logger::error(const std::string &message) {
  get_logger().error(message);
}

void Logger::warn(const std::string &message) {
  get_logger().warn(message);
}

void Logger::debug(const std::string &message) {
  get_logger().debug(message);
}