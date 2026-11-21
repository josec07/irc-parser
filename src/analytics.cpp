#include "ircp/analytics.h"

#include "ircp/util.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace ircp {
namespace {

constexpr std::size_t kTopN = 10;
constexpr std::size_t kSeenCap = 8192;

int toInt(const std::string& raw, int fallback) {
    if (raw.empty()) {
        return fallback;
    }
    try {
        std::size_t idx = 0;
        const int value = std::stoi(raw, &idx, 10);
        if (idx != raw.size()) {
            return fallback;
        }
        return value;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool hasBadge(const std::string& badges, const std::string& name) {
    std::size_t pos = 0;
    while (pos < badges.size()) {
        const std::size_t comma = badges.find(',', pos);
        const std::string item = badges.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const std::size_t slash = item.find('/');
        const std::string badge = slash == std::string::npos ? item : item.substr(0, slash);
        if (badge == name) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return false;
}

std::string commandName(std::string_view text) {
    constexpr std::string_view action = "\x01" "ACTION ";
    if (text.rfind(action, 0) == 0) {
        text.remove_prefix(action.size());
    }
    if (text.empty() || text[0] != '!') {
        return {};
    }
    std::size_t i = 1;
    while (i < text.size() && i <= 32) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c) && c != '_') {
            break;
        }
        ++i;
    }
    if (i == 1) {
        return {};
    }
    return asciiLower(text.substr(0, i));
}

std::vector<std::string> emoteUses(const std::string& tag) {
    std::vector<std::string> uses;
    std::size_t i = 0;
    while (i < tag.size()) {
        const std::size_t colon = tag.find(':', i);
        if (colon == std::string::npos || colon == i) {
            break;
        }
        const std::string id = tag.substr(i, colon - i);
        const std::size_t slash = tag.find('/', colon);
        const std::string ranges = tag.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1);
        int count = ranges.empty() ? 0 : 1;
        for (char c : ranges) {
            if (c == ',') {
                ++count;
            }
        }
        for (int n = 0; n < count; ++n) {
            uses.push_back(id);
        }
        if (slash == std::string::npos) {
            break;
        }
        i = slash + 1;
    }
    return uses;
}

std::vector<CountItem> topCounts(const std::unordered_map<std::string, std::int64_t>& counts) {
    std::vector<CountItem> items;
    items.reserve(counts.size());
    for (const auto& [name, count] : counts) {
        if (!name.empty() && count > 0) {
            items.push_back(CountItem{name, count});
        }
    }
    std::sort(items.begin(), items.end(), [](const CountItem& a, const CountItem& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.name < b.name;
    });
    if (items.size() > kTopN) {
        items.resize(kTopN);
    }
    return items;
}

const char* joinStateName(JoinState state) {
    switch (state) {
        case JoinState::Pending:
            return "pending";
        case JoinState::Joined:
            return "joined";
        case JoinState::Blocked:
            return "blocked";
    }
    return "pending";
}

}  // namespace

nlohmann::json snapshotJson(const ChannelSnapshot& snapshot, bool include_tops) {
    nlohmann::json body = {
        {"channel", snapshot.channel},
        {"join_state", joinStateName(snapshot.join_state)},
        {"block_reason", snapshot.block_reason},
        {"window_seconds", snapshot.window_seconds},
        {"generated_at", snapshot.generated_at},
        {"messages", snapshot.messages},
        {"messages_per_minute", snapshot.messages_per_minute},
        {"unique_chatters", snapshot.unique_chatters},
        {"first_messages", snapshot.first_messages},
        {"subscriber_messages", snapshot.subscriber_messages},
        {"moderator_messages", snapshot.moderator_messages},
        {"vip_messages", snapshot.vip_messages},
        {"broadcaster_messages", snapshot.broadcaster_messages},
        {"bits", snapshot.bits},
        {"commands", snapshot.commands},
        {"subscriptions", snapshot.subscriptions},
        {"resubscriptions", snapshot.resubscriptions},
        {"gifted_subscriptions", snapshot.gifted_subscriptions},
        {"raids", snapshot.raids},
        {"timeouts", snapshot.timeouts},
        {"bans", snapshot.bans},
        {"chat_clears", snapshot.chat_clears},
        {"other_notices", snapshot.other_notices},
        {"room",
         {{"room_id", snapshot.room_id},
          {"emote_only", snapshot.emote_only != 0},
          {"followers_only", snapshot.followers_only},
          {"slow_seconds", snapshot.slow_seconds},
          {"subs_only", snapshot.subs_only != 0},
          {"unique_chat", snapshot.unique_chat != 0}}},
    };
    if (include_tops) {
        nlohmann::json chatters = nlohmann::json::array();
        for (const CountItem& item : snapshot.top_chatters) {
            chatters.push_back({{"login", item.name}, {"messages", item.count}});
        }
        nlohmann::json commands = nlohmann::json::array();
        for (const CountItem& item : snapshot.top_commands) {
            commands.push_back({{"command", item.name}, {"count", item.count}});
        }
        nlohmann::json emotes = nlohmann::json::array();
        for (const CountItem& item : snapshot.top_emotes) {
            emotes.push_back({{"id", item.name}, {"count", item.count}});
        }
        body["top_chatters"] = std::move(chatters);
        body["top_commands"] = std::move(commands);
        body["top_emotes"] = std::move(emotes);
    }
    return body;
}

AnalyticsStore::AnalyticsStore(int window_seconds, Clock clock)
    : window_seconds_(window_seconds), clock_(std::move(clock)) {}

std::int64_t AnalyticsStore::clockMs() const {
    if (clock_) {
        return clock_();
    }
    return nowMs();
}

void AnalyticsStore::prune(Channel& channel) const {
    const std::int64_t cutoff = clockMs() - static_cast<std::int64_t>(window_seconds_) * 1000;
    auto& events = channel.events;
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const Event& event) { return event.ts < cutoff; }),
                 events.end());
}

void AnalyticsStore::remember(const std::string& id) {
    if (id.empty() || !seen_ids_.insert(id).second) {
        return;
    }
    seen_order_.push_back(id);
    while (seen_order_.size() > kSeenCap) {
        seen_ids_.erase(seen_order_.front());
        seen_order_.pop_front();
    }
}

bool AnalyticsStore::alreadySeen(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    if (seen_ids_.find(id) != seen_ids_.end()) {
        return true;
    }
    remember(id);
    return false;
}

void AnalyticsStore::ensureChannel(const std::string& channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_.emplace(channel, Channel{});
}

void AnalyticsStore::removeChannel(const std::string& channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_.erase(channel);
}

void AnalyticsStore::setJoinState(const std::string& channel, JoinState state, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return;
    }
    it->second.join_state = state;
    it->second.block_reason = state == JoinState::Blocked ? reason : std::string();
}

void AnalyticsStore::markAllPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [name, channel] : channels_) {
        (void)name;
        if (channel.join_state != JoinState::Blocked) {
            channel.join_state = JoinState::Pending;
        }
    }
}

void AnalyticsStore::ingest(const IrcMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (message.command == "ROOMSTATE") {
        if (message.params.empty()) {
            return;
        }
        const std::string channel = channelParam(message.params[0]);
        const auto it = channels_.find(channel);
        if (it == channels_.end()) {
            return;
        }
        Channel& slot = it->second;
        if (message.hasTag("room-id")) {
            slot.room_id = message.tag("room-id");
        }
        if (message.hasTag("emote-only")) {
            slot.emote_only = toInt(message.tag("emote-only"), slot.emote_only);
        }
        if (message.hasTag("followers-only")) {
            slot.followers_only = toInt(message.tag("followers-only"), slot.followers_only);
        }
        if (message.hasTag("slow")) {
            slot.slow_seconds = toInt(message.tag("slow"), slot.slow_seconds);
        }
        if (message.hasTag("subs-only")) {
            slot.subs_only = toInt(message.tag("subs-only"), slot.subs_only);
        }
        if (message.hasTag("r9k")) {
            slot.unique_chat = toInt(message.tag("r9k"), slot.unique_chat);
        }
        return;
    }

    if (message.command == "CLEARCHAT") {
        if (message.params.empty()) {
            return;
        }
        const std::string channel = channelParam(message.params[0]);
        const auto it = channels_.find(channel);
        if (it == channels_.end()) {
            return;
        }
        Channel& slot = it->second;
        prune(slot);
        const std::string target = asciiLower(message.has_trailing ? message.trailing : std::string());
        Event event;
        event.ts = clockMs();
        event.kind = Event::Kind::Moderation;
        if (target.empty()) {
            slot.events.erase(std::remove_if(slot.events.begin(), slot.events.end(),
                                             [](const Event& item) { return item.kind == Event::Kind::Message; }),
                              slot.events.end());
            event.notice = "clear";
        } else {
            slot.events.erase(std::remove_if(slot.events.begin(), slot.events.end(),
                                             [&](const Event& item) {
                                                 return item.kind == Event::Kind::Message && item.login == target;
                                             }),
                              slot.events.end());
            event.login = target;
            event.notice = message.hasTag("ban-duration") ? "timeout" : "ban";
        }
        slot.events.push_back(std::move(event));
        return;
    }

    if (message.command == "CLEARMSG") {
        if (message.params.empty()) {
            return;
        }
        const std::string channel = channelParam(message.params[0]);
        const auto it = channels_.find(channel);
        if (it == channels_.end()) {
            return;
        }
        const std::string id = message.tag("target-msg-id");
        if (id.empty()) {
            return;
        }
        auto& events = it->second.events;
        events.erase(std::remove_if(events.begin(), events.end(),
                                    [&](const Event& item) {
                                        return item.kind == Event::Kind::Message && item.id == id;
                                    }),
                     events.end());
        return;
    }

    if (message.command != "PRIVMSG" && message.command != "USERNOTICE") {
        return;
    }
    if (message.params.empty()) {
        return;
    }
    const std::string channel = channelParam(message.params[0]);
    const auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return;
    }

    std::string dedup = message.tag("source-id");
    if (dedup.empty()) {
        dedup = message.tag("id");
    }
    if (alreadySeen(dedup)) {
        return;
    }

    if (message.hasTag("room-id") && it->second.room_id.empty()) {
        it->second.room_id = message.tag("room-id");
    }

    Event event;
    event.ts = clockMs();
    event.id = message.tag("id");
    event.login = asciiLower(message.command == "USERNOTICE" ? message.tag("login") : message.nick);
    if (event.login.empty()) {
        event.login = asciiLower(message.nick);
    }

    if (message.command == "USERNOTICE") {
        event.kind = Event::Kind::Notice;
        event.notice = message.tag("msg-id");
        it->second.events.push_back(std::move(event));
        prune(it->second);
        return;
    }

    event.kind = Event::Kind::Message;
    event.first = message.tag("first-msg") == "1";
    event.subscriber = message.tag("subscriber") == "1";
    event.moderator = message.tag("mod") == "1";
    event.vip = message.hasTag("vip") && message.tag("vip") != "0";
    event.broadcaster = hasBadge(message.tag("badges"), "broadcaster") || event.login == channel;
    event.bits = std::max(0, toInt(message.tag("bits"), 0));
    event.command = commandName(message.has_trailing ? message.trailing : std::string());
    event.emotes = emoteUses(message.tag("emotes"));
    it->second.events.push_back(std::move(event));
    prune(it->second);
}

std::vector<std::string> AnalyticsStore::channels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(channels_.size());
    for (const auto& [name, channel] : channels_) {
        (void)channel;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

ChannelSnapshot AnalyticsStore::buildSnapshot(const std::string& name, const Channel& channel) const {
    ChannelSnapshot snapshot;
    snapshot.channel = name;
    snapshot.join_state = channel.join_state;
    snapshot.block_reason = channel.block_reason;
    snapshot.room_id = channel.room_id;
    snapshot.emote_only = channel.emote_only;
    snapshot.followers_only = channel.followers_only;
    snapshot.slow_seconds = channel.slow_seconds;
    snapshot.subs_only = channel.subs_only;
    snapshot.unique_chat = channel.unique_chat;
    snapshot.window_seconds = window_seconds_;
    snapshot.generated_at = clockMs() / 1000;

    std::unordered_set<std::string> chatters;
    std::unordered_map<std::string, std::int64_t> chatter_counts;
    std::unordered_map<std::string, std::int64_t> command_counts;
    std::unordered_map<std::string, std::int64_t> emote_counts;

    for (const Event& event : channel.events) {
        if (event.kind == Event::Kind::Moderation) {
            if (event.notice == "timeout") {
                ++snapshot.timeouts;
            } else if (event.notice == "ban") {
                ++snapshot.bans;
            } else if (event.notice == "clear") {
                ++snapshot.chat_clears;
            }
            continue;
        }
        if (event.kind == Event::Kind::Notice) {
            if (event.notice == "sub") {
                ++snapshot.subscriptions;
            } else if (event.notice == "resub") {
                ++snapshot.resubscriptions;
            } else if (event.notice == "subgift" || event.notice == "anonsubgift" ||
                       event.notice == "submysterygift") {
                ++snapshot.gifted_subscriptions;
            } else if (event.notice == "raid") {
                ++snapshot.raids;
            } else if (!event.notice.empty()) {
                ++snapshot.other_notices;
            }
            continue;
        }

        ++snapshot.messages;
        if (!event.login.empty()) {
            chatters.insert(event.login);
            ++chatter_counts[event.login];
        }
        if (event.first) {
            ++snapshot.first_messages;
        }
        if (event.subscriber) {
            ++snapshot.subscriber_messages;
        }
        if (event.moderator) {
            ++snapshot.moderator_messages;
        }
        if (event.vip) {
            ++snapshot.vip_messages;
        }
        if (event.broadcaster) {
            ++snapshot.broadcaster_messages;
        }
        snapshot.bits += event.bits;
        if (!event.command.empty()) {
            ++snapshot.commands;
            ++command_counts[event.command];
        }
        for (const std::string& emote : event.emotes) {
            ++emote_counts[emote];
        }
    }

    snapshot.unique_chatters = static_cast<std::int64_t>(chatters.size());
    snapshot.messages_per_minute =
        window_seconds_ == 0 ? 0.0
                             : static_cast<double>(snapshot.messages) * 60.0 / static_cast<double>(window_seconds_);
    snapshot.top_chatters = topCounts(chatter_counts);
    snapshot.top_commands = topCounts(command_counts);
    snapshot.top_emotes = topCounts(emote_counts);
    return snapshot;
}

std::optional<ChannelSnapshot> AnalyticsStore::snapshot(const std::string& channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return std::nullopt;
    }
    Channel copy = it->second;
    prune(copy);
    return buildSnapshot(channel, copy);
}

nlohmann::json AnalyticsStore::snapshotJson(const std::string& channel) const {
    const auto snap = snapshot(channel);
    if (!snap) {
        return nlohmann::json::object();
    }
    return ircp::snapshotJson(*snap, true);
}

nlohmann::json AnalyticsStore::listJson() const {
    nlohmann::json body;
    body["channels"] = nlohmann::json::array();
    for (const std::string& name : channels()) {
        const auto snap = snapshot(name);
        if (!snap) {
            continue;
        }
        body["channels"].push_back(ircp::snapshotJson(*snap, false));
    }
    return body;
}

}  // namespace ircp
