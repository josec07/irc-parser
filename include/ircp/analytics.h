#pragma once

#include "ircp/irc_message.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ircp {

enum class JoinState { Pending, Joined, Blocked };

struct CountItem {
    std::string name;
    std::int64_t count = 0;
};

struct ChannelSnapshot {
    std::string channel;
    JoinState join_state = JoinState::Pending;
    std::string block_reason;
    std::string room_id;
    int emote_only = 0;
    int followers_only = -1;
    int slow_seconds = 0;
    int subs_only = 0;
    int unique_chat = 0;
    std::int64_t messages = 0;
    double messages_per_minute = 0;
    std::int64_t unique_chatters = 0;
    std::int64_t first_messages = 0;
    std::int64_t subscriber_messages = 0;
    std::int64_t moderator_messages = 0;
    std::int64_t vip_messages = 0;
    std::int64_t broadcaster_messages = 0;
    std::int64_t bits = 0;
    std::int64_t commands = 0;
    std::int64_t subscriptions = 0;
    std::int64_t resubscriptions = 0;
    std::int64_t gifted_subscriptions = 0;
    std::int64_t raids = 0;
    std::int64_t timeouts = 0;
    std::int64_t bans = 0;
    std::int64_t chat_clears = 0;
    std::int64_t other_notices = 0;
    int window_seconds = 0;
    std::int64_t generated_at = 0;
    std::vector<CountItem> top_chatters;
    std::vector<CountItem> top_commands;
    std::vector<CountItem> top_emotes;
};

nlohmann::json snapshotJson(const ChannelSnapshot& snapshot, bool include_tops);

// In-memory rolling window. There is no text field and no disk write.
// CLEARCHAT / CLEARMSG drop the matching events so a moderation action is
// reflected in the counts immediately.
class AnalyticsStore {
public:
    using Clock = std::function<std::int64_t()>;

    explicit AnalyticsStore(int window_seconds, Clock clock = {});

    void ensureChannel(const std::string& channel);
    void removeChannel(const std::string& channel);
    void setJoinState(const std::string& channel, JoinState state, const std::string& reason = {});
    void markAllPending();
    void ingest(const IrcMessage& message);

    std::vector<std::string> channels() const;
    std::optional<ChannelSnapshot> snapshot(const std::string& channel) const;
    nlohmann::json snapshotJson(const std::string& channel) const;
    nlohmann::json listJson() const;

private:
    struct Event {
        enum class Kind { Message, Notice, Moderation };
        std::int64_t ts = 0;
        Kind kind = Kind::Message;
        std::string id;
        std::string login;
        bool first = false;
        bool subscriber = false;
        bool moderator = false;
        bool vip = false;
        bool broadcaster = false;
        int bits = 0;
        std::string command;
        std::vector<std::string> emotes;
        std::string notice;
    };

    struct Channel {
        JoinState join_state = JoinState::Pending;
        std::string block_reason;
        std::string room_id;
        int emote_only = 0;
        int followers_only = -1;
        int slow_seconds = 0;
        int subs_only = 0;
        int unique_chat = 0;
        std::vector<Event> events;
    };

    std::int64_t clockMs() const;
    void prune(Channel& channel) const;
    void remember(const std::string& id);
    bool alreadySeen(const std::string& id);
    ChannelSnapshot buildSnapshot(const std::string& name, const Channel& channel) const;

    int window_seconds_;
    Clock clock_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Channel> channels_;
    std::unordered_set<std::string> seen_ids_;
    std::deque<std::string> seen_order_;
};

}  // namespace ircp
