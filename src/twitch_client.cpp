#include "ircp/twitch_client.h"

#include "ircp/compliance.h"
#include "ircp/irc_message.h"
#include "ircp/logger.h"
#include "ircp/util.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ircp {
namespace {

static_assert(Compliance::kReadOnly, "irc-parser is read-only");
static_assert(Compliance::kUsesTls, "irc-parser speaks TLS only");
static_assert(!Compliance::kRequestsMembership, "membership JOINs are not requested");
static_assert(!Compliance::kHostConfigurable, "the IRC host is fixed");
static_assert(!Compliance::kRetainsMessageText, "message text is not retained");
static_assert(!Compliance::kPersistsChat, "chat is not persisted");

bool isBlockedNotice(const std::string& msg_id) {
    return msg_id == "msg_banned" || msg_id == "msg_channel_suspended" || msg_id == "msg_channel_blocked" ||
           msg_id == "tos_ban";
}

bool startsWith(const std::string& line, const char* word) {
    const std::size_t n = std::char_traits<char>::length(word);
    return line.size() >= n && line.compare(0, n, word) == 0 && (line.size() == n || line[n] == ' ');
}

}  // namespace

TwitchClient::TwitchClient(const Config& config, AnalyticsStore& analytics)
    : nick_(config.nick()),
      token_(config.token()),
      max_channels_(config.maxChannels()),
      window_seconds_(config.windowSeconds()),
      analytics_(analytics) {
    for (const std::string& channel : config.channels()) {
        desired_.insert(channel);
        analytics_.ensureChannel(channel);
    }
}

TwitchClient::~TwitchClient() { stop(); }

void TwitchClient::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void TwitchClient::stop() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    started_ = false;
}

WatchStatus TwitchClient::watch(const std::string& channel) {
    std::string error;
    const auto login = parseLogin(channel, error);
    if (!login || *login != channel) {
        return WatchStatus::Invalid;
    }
    bool reset = false;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (blocked_.erase(channel) > 0) {
            desired_.insert(channel);
            reset = true;
        } else if (desired_.count(channel) != 0) {
            return WatchStatus::Duplicate;
        } else if (static_cast<int>(desired_.size()) >= max_channels_) {
            return WatchStatus::AtCap;
        } else {
            desired_.insert(channel);
        }
    }
    analytics_.ensureChannel(channel);
    analytics_.setJoinState(channel, JoinState::Pending);
    return reset ? WatchStatus::Reset : WatchStatus::Created;
}

bool TwitchClient::unwatch(const std::string& channel) {
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        const std::size_t removed = desired_.erase(channel) + blocked_.erase(channel);
        if (removed == 0) {
            return false;
        }
    }
    analytics_.removeChannel(channel);
    return true;
}

nlohmann::json TwitchClient::statusJson() const {
    std::lock_guard<std::mutex> lock(state_mu_);
    nlohmann::json scopes = nlohmann::json::array();
    for (const std::string& scope : scopes_) {
        scopes.push_back(scope);
    }
    return {{"connected", connected_},
            {"authenticated", authenticated_},
            {"auth_rejected", auth_rejected_},
            {"nick", nick_},
            {"host", Compliance::kIrcHost},
            {"port", Compliance::kIrcPort},
            {"tls", Compliance::kUsesTls},
            {"read_only", Compliance::kReadOnly},
            {"membership_capability", Compliance::kRequestsMembership},
            {"persists_chat", Compliance::kPersistsChat},
            {"message_text_retained", Compliance::kRetainsMessageText},
            {"channels", static_cast<int>(desired_.size())},
            {"max_channels", max_channels_},
            {"window_seconds", window_seconds_},
            {"token_expires_in", token_expires_in_},
            {"scopes", std::move(scopes)},
            {"last_error", last_error_}};
}

void TwitchClient::sleepInterruptible(int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!stop_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool TwitchClient::sendLine(const std::string& line, bool sensitive, std::string& error) {
    const bool allowed = startsWith(line, "CAP") || startsWith(line, "PASS") || startsWith(line, "NICK") ||
                         startsWith(line, "JOIN") || startsWith(line, "PART") || startsWith(line, "PONG") ||
                         startsWith(line, "QUIT");
    if (!allowed) {
        error = "blocked outbound irc command";
        Logger::error(error);
        return false;
    }
    if (sensitive || startsWith(line, "PASS")) {
        Logger::debug("-> PASS oauth:<redacted>");
    } else {
        Logger::debug(std::string("-> ") + line);
    }
    return session_.writeLine(line, error);
}

bool TwitchClient::validateToken(std::string& error, bool& rejected) {
    rejected = false;
    int status = 0;
    std::string body;
    const std::vector<std::pair<std::string, std::string>> headers = {{"Authorization", "OAuth " + token_}};
    if (!TlsSession::httpsGet(Compliance::kValidateHost, "/oauth2/validate", headers, status, body, error)) {
        error = "token validate request failed: " + error;
        return false;
    }
    if (status == 401 || status == 403) {
        rejected = true;
        error = "Twitch rejected the OAuth token. Create a user token with the chat:read scope and restart.";
        return false;
    }
    if (status != 200) {
        error = "token validate returned HTTP " + std::to_string(status);
        return false;
    }

    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "token validate returned invalid JSON";
        return false;
    }
    const std::string login = asciiLower(parsed.value("login", ""));
    if (login != nick_) {
        rejected = true;
        error = "TWITCH_NICK does not match the token login. NICK must be the account that granted the token.";
        return false;
    }
    std::vector<std::string> scopes;
    bool chat_read = false;
    bool chat_edit = false;
    if (parsed.contains("scopes") && parsed["scopes"].is_array()) {
        for (const auto& scope : parsed["scopes"]) {
            if (!scope.is_string()) {
                continue;
            }
            const std::string value = scope.get<std::string>();
            if (value == "chat:read") {
                chat_read = true;
            }
            if (value == "chat:edit") {
                chat_edit = true;
            }
            scopes.push_back(value);
        }
    }
    if (!chat_read) {
        rejected = true;
        error = "token is missing the chat:read scope. Do not request chat:edit; this process never sends chat.";
        return false;
    }
    if (chat_edit) {
        Logger::warn("token includes chat:edit, which this process does not use. Prefer a chat:read-only token.");
    }

    const int expires = parsed.value("expires_in", -1);
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        token_expires_in_ = expires;
        scopes_ = scopes;
        auth_rejected_ = false;
    }
    if (expires >= 0 && expires < 3600) {
        Logger::warn("OAuth token expires in under an hour");
    }
    Logger::info("validated Twitch token for " + nick_ + " (chat:read)");
    return true;
}

bool TwitchClient::connectAndAuth(std::string& error, bool& rejected) {
    rejected = false;
    if (!session_.connect(Compliance::kIrcHost, Compliance::kIrcPort, error)) {
        return false;
    }
    // tags + commands only. membership would deliver JOIN/PART presence we do not use.
    if (!sendLine("CAP REQ :twitch.tv/tags twitch.tv/commands", false, error) ||
        !sendLine("PASS oauth:" + token_, true, error) || !sendLine("NICK " + nick_, false, error)) {
        session_.close();
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!stop_ && std::chrono::steady_clock::now() < deadline) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        std::string line;
        const int rc = session_.readLine(line, static_cast<int>(left < 1 ? 1 : left), error);
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            session_.close();
            return false;
        }
        const auto message = IrcParser::parse(line);
        if (!message) {
            continue;
        }
        if (message->command == "PING") {
            if (!sendLine("PONG " + ircp::pongPayload(*message), false, error)) {
                session_.close();
                return false;
            }
            continue;
        }
        if (message->command == "NOTICE") {
            const std::string text = message->has_trailing ? message->trailing : std::string();
            if (text.find("Login authentication failed") != std::string::npos ||
                text.find("Improperly formatted auth") != std::string::npos) {
                rejected = true;
                error = "Twitch IRC rejected the login. Check TWITCH_NICK and TWITCH_OAUTH_TOKEN.";
                session_.close();
                return false;
            }
        }
        if (message->command == "001") {
            return true;
        }
    }
    error = "timed out waiting for Twitch IRC welcome";
    session_.close();
    return false;
}

void TwitchClient::paceJoin() {
    using clock = std::chrono::steady_clock;
    for (;;) {
        if (stop_) {
            return;
        }
        const auto now = clock::now();
        int wait_ms = 0;
        {
            std::lock_guard<std::mutex> lock(pace_mu_);
            const auto window = std::chrono::seconds(10);
            join_times_.erase(std::remove_if(join_times_.begin(), join_times_.end(),
                                             [&](const clock::time_point& ts) { return now - ts > window; }),
                              join_times_.end());
            if (static_cast<int>(join_times_.size()) >= Compliance::kMaxJoinsPer10s) {
                wait_ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(join_times_.front() + window - now).count());
            } else if (!join_times_.empty()) {
                const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - join_times_.back()).count();
                if (since < Compliance::kJoinSpacingMs) {
                    wait_ms = Compliance::kJoinSpacingMs - static_cast<int>(since);
                }
            }
            if (wait_ms <= 0) {
                join_times_.push_back(now);
                return;
            }
        }
        sleepInterruptible(wait_ms);
    }
}

void TwitchClient::syncChannels() {
    std::unordered_set<std::string> desired;
    std::unordered_set<std::string> blocked;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        desired = desired_;
        blocked = blocked_;
    }

    std::string error;
    for (auto it = on_wire_.begin(); it != on_wire_.end();) {
        if (desired.count(*it) == 0) {
            if (!sendLine("PART #" + *it, false, error)) {
                Logger::warn("part failed for #" + *it + ": " + error);
                return;
            }
            Logger::info("leaving #" + *it);
            it = on_wire_.erase(it);
        } else {
            ++it;
        }
    }

    for (const std::string& channel : desired) {
        if (stop_ || !session_.isOpen()) {
            return;
        }
        if (blocked.count(channel) != 0 || on_wire_.count(channel) != 0) {
            continue;
        }
        paceJoin();
        if (!sendLine("JOIN #" + channel, false, error)) {
            Logger::warn("join failed for #" + channel + ": " + error);
            return;
        }
        on_wire_.insert(channel);
        analytics_.setJoinState(channel, JoinState::Pending);
        Logger::info("joining #" + channel);
    }
}

void TwitchClient::handleLine(const std::string& line) {
    const auto message = IrcParser::parse(line);
    if (!message) {
        return;
    }

    // Never log trailing text. Docker captures stderr, and that would be a chat log.
    if (!message->params.empty() && (message->command == "PRIVMSG" || message->command == "USERNOTICE" ||
                                     message->command == "CLEARCHAT" || message->command == "CLEARMSG")) {
        Logger::debug(message->command + " #" + channelParam(message->params[0]));
    } else if (message->command != "PRIVMSG") {
        Logger::debug(message->command);
    }

    if (message->command == "PING") {
        std::string error;
        if (!sendLine("PONG " + ircp::pongPayload(*message), false, error)) {
            Logger::warn("failed to send PONG: " + error);
            session_.close();
        }
        return;
    }

    if (message->command == "RECONNECT") {
        Logger::info("Twitch requested reconnect");
        session_.close();
        return;
    }

    if (message->command == "353") {
        return;
    }

    if (message->command == "JOIN") {
        if (asciiLower(message->nick) == nick_ && !message->params.empty()) {
            analytics_.setJoinState(channelParam(message->params[0]), JoinState::Joined);
        }
        return;
    }

    if (message->command == "366" && message->params.size() >= 2) {
        analytics_.setJoinState(channelParam(message->params[1]), JoinState::Joined);
        return;
    }

    if (message->command == "NOTICE") {
        const std::string msg_id = message->tag("msg-id");
        std::string channel;
        if (!message->params.empty()) {
            channel = channelParam(message->params[0]);
        }
        if (!channel.empty() && isBlockedNotice(msg_id)) {
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                blocked_.insert(channel);
            }
            on_wire_.erase(channel);
            analytics_.setJoinState(channel, JoinState::Blocked, msg_id);
            Logger::warn("not rejoining #" + channel + " after notice " + msg_id);
        }
        return;
    }

    analytics_.ingest(*message);
}

void TwitchClient::run() {
    int backoff_ms = 2000;
    bool validated = false;

    while (!stop_) {
        if (!validated) {
            std::string error;
            bool rejected = false;
            if (!validateToken(error, rejected)) {
                {
                    std::lock_guard<std::mutex> lock(state_mu_);
                    last_error_ = error;
                    auth_rejected_ = rejected;
                    authenticated_ = false;
                    connected_ = false;
                }
                Logger::error(error);
                if (rejected) {
                    while (!stop_) {
                        sleepInterruptible(1000);
                    }
                    return;
                }
                sleepInterruptible(std::min(backoff_ms, 300000));
                backoff_ms = std::min(backoff_ms * 2, 300000);
                continue;
            }
            validated = true;
            backoff_ms = 2000;
        }

        std::string error;
        bool rejected = false;
        if (!connectAndAuth(error, rejected)) {
            session_.close();
            on_wire_.clear();
            analytics_.markAllPending();
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                connected_ = false;
                authenticated_ = false;
                last_error_ = error;
                auth_rejected_ = rejected;
            }
            Logger::error(error);
            if (rejected) {
                while (!stop_) {
                    sleepInterruptible(1000);
                }
                return;
            }
            sleepInterruptible(backoff_ms);
            backoff_ms = std::min(backoff_ms * 2, 60000);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(state_mu_);
            connected_ = true;
            authenticated_ = true;
            auth_rejected_ = false;
            last_error_.clear();
        }
        backoff_ms = 2000;
        Logger::info(std::string("connected to ") + Compliance::kIrcHost + ":" + std::to_string(Compliance::kIrcPort));

        while (!stop_ && session_.isOpen()) {
            syncChannels();

            std::string line;
            std::string read_error;
            const int rc = session_.readLine(line, 250, read_error);
            if (rc == 0) {
                continue;
            }
            if (rc < 0) {
                Logger::warn(std::string("irc connection closed: ") + read_error);
                session_.close();
                break;
            }
            handleLine(line);
        }

        session_.close();
        on_wire_.clear();
        analytics_.markAllPending();
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            connected_ = false;
            authenticated_ = false;
            if (last_error_.empty()) {
                last_error_ = "disconnected";
            }
        }
        if (!stop_) {
            Logger::info("reconnecting after " + std::to_string(backoff_ms) + "ms");
            sleepInterruptible(backoff_ms);
            backoff_ms = std::min(backoff_ms * 2, 60000);
        }
    }

    if (session_.isOpen()) {
        std::string error;
        sendLine("QUIT :irc-parser shutting down", false, error);
        session_.close();
    }
}

}  // namespace ircp
