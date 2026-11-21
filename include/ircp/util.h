#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ircp {

std::string asciiLower(std::string_view in);
std::string trim(std::string_view in);
bool constantTimeEqual(std::string_view a, std::string_view b);
std::int64_t nowMs();

// Twitch logins are 4-25 characters: letters, digits, underscore.
std::optional<std::string> parseLogin(std::string_view input, std::string& error);

// A login, a #login, or a https://www.twitch.tv/login URL. Nothing else.
// Other hosts are rejected so this cannot be pointed at an arbitrary server.
std::optional<std::string> parseChannel(std::string_view input, std::string& error);

std::optional<std::string> normalizeToken(std::string_view input, std::string& error);

}  // namespace ircp
