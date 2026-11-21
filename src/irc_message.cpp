#include "ircp/irc_message.h"

#include "ircp/util.h"

#include <cctype>

namespace ircp {
namespace {

std::string asciiUpper(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c - 'a' + 'A'));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

}  // namespace

const std::string* IrcMessage::findTag(const std::string& key) const {
    const auto it = tags.find(key);
    if (it == tags.end()) {
        return nullptr;
    }
    return &it->second;
}

std::string IrcMessage::tag(const std::string& key) const {
    const auto* value = findTag(key);
    return value == nullptr ? std::string() : *value;
}

bool IrcMessage::hasTag(const std::string& key) const { return tags.find(key) != tags.end(); }

std::string IrcParser::unescapeTag(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            switch (next) {
                case ':':
                    out.push_back(';');
                    break;
                case 's':
                    out.push_back(' ');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                default:
                    out.push_back(next);
                    break;
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::optional<IrcMessage> IrcParser::parse(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return std::nullopt;
    }

    IrcMessage msg;
    std::size_t i = 0;

    if (line[i] == '@') {
        ++i;
        const std::size_t tag_end = line.find(' ', i);
        if (tag_end == std::string_view::npos) {
            return msg;
        }
        std::string_view tags = line.substr(i, tag_end - i);
        i = tag_end + 1;
        while (!tags.empty()) {
            const std::size_t semi = tags.find(';');
            const std::string_view item = tags.substr(0, semi);
            const std::size_t eq = item.find('=');
            std::string key(item.substr(0, eq));
            std::string value;
            if (eq != std::string_view::npos) {
                value = unescapeTag(item.substr(eq + 1));
            }
            if (!key.empty()) {
                msg.tags.emplace(std::move(key), std::move(value));
            }
            if (semi == std::string_view::npos) {
                break;
            }
            tags.remove_prefix(semi + 1);
        }
    }

    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    if (i >= line.size()) {
        return msg;
    }

    if (line[i] == ':') {
        ++i;
        const std::size_t prefix_end = line.find(' ', i);
        msg.prefix = std::string(line.substr(i, prefix_end == std::string_view::npos ? std::string_view::npos
                                                                                      : prefix_end - i));
        const std::size_t bang = msg.prefix.find('!');
        if (bang != std::string::npos) {
            msg.nick = msg.prefix.substr(0, bang);
        }
        if (prefix_end == std::string_view::npos) {
            return msg;
        }
        i = prefix_end + 1;
    }

    while (i < line.size() && line[i] == ' ') {
        ++i;
    }
    if (i >= line.size()) {
        return msg;
    }

    const std::size_t cmd_end = line.find(' ', i);
    msg.command = asciiUpper(line.substr(i, cmd_end == std::string_view::npos ? std::string_view::npos : cmd_end - i));
    if (cmd_end == std::string_view::npos) {
        return msg;
    }
    i = cmd_end + 1;

    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        if (line[i] == ':') {
            msg.has_trailing = true;
            msg.trailing = std::string(line.substr(i + 1));
            msg.params.push_back(msg.trailing);
            break;
        }
        const std::size_t param_end = line.find(' ', i);
        msg.params.emplace_back(line.substr(i, param_end == std::string_view::npos ? std::string_view::npos
                                                                                   : param_end - i));
        if (param_end == std::string_view::npos) {
            break;
        }
        i = param_end + 1;
    }

    return msg;
}

std::string pongPayload(const IrcMessage& message) {
    std::string payload = message.has_trailing ? message.trailing
                                               : (message.params.empty() ? std::string() : message.params[0]);
    if (!payload.empty() && payload[0] == ':') {
        payload.erase(payload.begin());
    }
    if (payload.empty() || payload.size() > 64) {
        return ":tmi.twitch.tv";
    }
    for (unsigned char c : payload) {
        if (std::isalnum(c) == 0 && c != '.' && c != '-' && c != '_' && c != ':') {
            return ":tmi.twitch.tv";
        }
    }
    return ":" + payload;
}

std::string channelParam(std::string_view raw) {
    if (raw.empty() || raw == "*") {
        return {};
    }
    std::string channel(raw);
    if (!channel.empty() && channel[0] == '#') {
        channel.erase(channel.begin());
    }
    return asciiLower(channel);
}

}  // namespace ircp
