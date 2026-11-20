#include "ircp/config.h"

#include "ircp/compliance.h"
#include "ircp/logger.h"
#include "ircp/util.h"

#include <arpa/inet.h>
#include <cstdlib>
#include <stdexcept>
#include <unordered_set>

namespace ircp {
namespace {

std::string envOr(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr) {
        return {};
    }
    return trim(value);
}

int parseIntEnv(const std::string& raw, const char* key) {
    if (raw.empty()) {
        throw std::runtime_error(std::string(key) + " is empty");
    }
    std::size_t idx = 0;
    int value = 0;
    try {
        value = std::stoi(raw, &idx, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(key) + " is not an integer");
    }
    if (idx != raw.size()) {
        throw std::runtime_error(std::string(key) + " is not an integer");
    }
    return value;
}

std::vector<std::string> splitList(const std::string& raw) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : raw) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

}  // namespace

Config Config::fromEnvironment() {
    const std::string level = envOr("IRCP_LOG_LEVEL");
    if (!level.empty()) {
        Logger::setLevel(asciiLower(level));
    }

    Config config;

    config.bind_ = envOr("IRCP_BIND");
    if (config.bind_.empty()) {
        config.bind_ = "127.0.0.1";
    }
    in_addr addr{};
    if (inet_pton(AF_INET, config.bind_.c_str(), &addr) != 1) {
        throw std::runtime_error("IRCP_BIND must be an IPv4 address (default 127.0.0.1)");
    }

    const std::string port_raw = envOr("IRCP_PORT");
    if (!port_raw.empty()) {
        config.port_ = parseIntEnv(port_raw, "IRCP_PORT");
    }
    if (config.port_ < 1 || config.port_ > 65535) {
        throw std::runtime_error("IRCP_PORT must be between 1 and 65535");
    }

    config.api_key_ = envOr("IRCP_API_KEY");
    if (config.api_key_.size() < 8) {
        throw std::runtime_error("IRCP_API_KEY is required and must be at least 8 characters");
    }
    for (unsigned char c : config.api_key_) {
        if (c <= 0x20 || c == 0x7f) {
            throw std::runtime_error("IRCP_API_KEY must not contain whitespace or control characters");
        }
    }
    if (config.api_key_.size() < 16) {
        Logger::warn("IRCP_API_KEY is shorter than 16 characters");
    }

    std::string error;
    const auto nick = parseLogin(envOr("TWITCH_NICK"), error);
    if (!nick) {
        throw std::runtime_error("TWITCH_NICK: " + error);
    }
    config.nick_ = *nick;

    const auto token = normalizeToken(envOr("TWITCH_OAUTH_TOKEN"), error);
    if (!token) {
        throw std::runtime_error(error);
    }
    config.token_ = *token;

    const std::string window_raw = envOr("IRCP_WINDOW_SECONDS");
    if (!window_raw.empty()) {
        config.window_seconds_ = parseIntEnv(window_raw, "IRCP_WINDOW_SECONDS");
    }
    if (config.window_seconds_ < 60 || config.window_seconds_ > Compliance::kHardMaxWindowSeconds) {
        throw std::runtime_error(
            "IRCP_WINDOW_SECONDS must be between 60 and 86400. "
            "Chat is not stored on disk, and the in-memory window is capped at 24 hours "
            "so this process cannot become a chat archive.");
    }

    const std::string max_raw = envOr("IRCP_MAX_CHANNELS");
    if (!max_raw.empty()) {
        config.max_channels_ = parseIntEnv(max_raw, "IRCP_MAX_CHANNELS");
    }
    if (config.max_channels_ < 1 || config.max_channels_ > Compliance::kHardMaxChannels) {
        throw std::runtime_error(
            "IRCP_MAX_CHANNELS must be between 1 and 100. "
            "Twitch limits a normal account to 100 concurrent chat joins.");
    }

    std::unordered_set<std::string> seen;
    for (const std::string& item : splitList(envOr("IRCP_CHANNELS"))) {
        const auto channel = parseChannel(item, error);
        if (!channel) {
            throw std::runtime_error("IRCP_CHANNELS: " + error);
        }
        if (seen.insert(*channel).second) {
            config.channels_.push_back(*channel);
        }
    }
    if (static_cast<int>(config.channels_.size()) > config.max_channels_) {
        throw std::runtime_error("IRCP_CHANNELS lists more channels than IRCP_MAX_CHANNELS");
    }

    return config;
}

}  // namespace ircp
