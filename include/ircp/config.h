#pragma once

#include <string>
#include <vector>

namespace ircp {

class Config {
public:
    static Config fromEnvironment();

    const std::string& bindAddress() const { return bind_; }
    int port() const { return port_; }
    const std::string& apiKey() const { return api_key_; }
    const std::string& nick() const { return nick_; }
    const std::string& token() const { return token_; }
    const std::vector<std::string>& channels() const { return channels_; }
    int windowSeconds() const { return window_seconds_; }
    int maxChannels() const { return max_channels_; }

private:
    Config() = default;

    std::string bind_;
    int port_ = 8080;
    std::string api_key_;
    std::string nick_;
    std::string token_;
    std::vector<std::string> channels_;
    int window_seconds_ = 900;
    int max_channels_ = 20;
};

}  // namespace ircp
