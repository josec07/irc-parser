#pragma once

#include "ircp/analytics.h"
#include "ircp/config.h"
#include "ircp/tls_session.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ircp {

enum class WatchStatus { Created, Reset, Duplicate, AtCap, Invalid };

// Read-only Twitch IRC client. Outbound commands are an allowlist:
// CAP, PASS, NICK, JOIN, PART, PONG, QUIT. There is no method that sends
// PRIVMSG, WHISPER, or a moderation command.
class TwitchClient {
public:
    TwitchClient(const Config& config, AnalyticsStore& analytics);
    ~TwitchClient();

    TwitchClient(const TwitchClient&) = delete;
    TwitchClient& operator=(const TwitchClient&) = delete;

    void start();
    void stop();

    WatchStatus watch(const std::string& channel);
    bool unwatch(const std::string& channel);
    nlohmann::json statusJson() const;

private:
    void run();
    bool validateToken(std::string& error, bool& rejected);
    bool connectAndAuth(std::string& error, bool& rejected);
    bool sendLine(const std::string& line, bool sensitive, std::string& error);
    void handleLine(const std::string& line);
    void paceJoin();
    void syncChannels();
    void sleepInterruptible(int ms);

    const std::string nick_;
    const std::string token_;
    const int max_channels_;
    const int window_seconds_;
    AnalyticsStore& analytics_;

    TlsSession session_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> started_{false};

    mutable std::mutex state_mu_;
    std::unordered_set<std::string> desired_;
    std::unordered_set<std::string> blocked_;
    bool connected_ = false;
    bool authenticated_ = false;
    bool auth_rejected_ = false;
    std::string last_error_;
    int token_expires_in_ = -1;
    std::vector<std::string> scopes_;

    std::unordered_set<std::string> on_wire_;

    std::mutex pace_mu_;
    std::vector<std::chrono::steady_clock::time_point> join_times_;
};

}  // namespace ircp
