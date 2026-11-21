#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ircp {

std::string demoPageHtml();

// Public recruiter view. Counts only. Drops chatter names, command names,
// emote ids, room ids, the bot nick, and anything that looks like a token.
nlohmann::json demoSnapshot(const nlohmann::json& status, const nlohmann::json& channel_list);

}  // namespace ircp
