#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ircp {

struct IrcMessage {
    std::unordered_map<std::string, std::string> tags;
    std::string prefix;
    std::string nick;
    std::string command;
    std::vector<std::string> params;
    std::string trailing;
    bool has_trailing = false;

    const std::string* findTag(const std::string& key) const;
    std::string tag(const std::string& key) const;
    bool hasTag(const std::string& key) const;
};

class IrcParser {
public:
    // Returns nullopt for an empty line. Does not throw on malformed input;
    // a best-effort message is returned so the connection can keep reading.
    static std::optional<IrcMessage> parse(std::string_view line);

    static std::string unescapeTag(std::string_view value);
};

std::string channelParam(std::string_view raw);
std::string pongPayload(const IrcMessage& message);

}  // namespace ircp
