#include "ircp/util.h"

#include <cctype>
#include <chrono>

namespace ircp {
namespace {

bool isLoginChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool isLoginShape(std::string_view value) {
    if (value.size() < 4 || value.size() > 25) {
        return false;
    }
    for (char c : value) {
        if (!isLoginChar(c)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string asciiLower(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string trim(std::string_view in) {
    std::size_t begin = 0;
    while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin]))) {
        ++begin;
    }
    std::size_t end = in.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1]))) {
        --end;
    }
    return std::string(in.substr(begin, end - begin));
}

bool constantTimeEqual(std::string_view a, std::string_view b) {
    unsigned char diff = static_cast<unsigned char>(a.size() ^ b.size());
    const std::size_t n = a.size() > b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff = static_cast<unsigned char>(diff | (ca ^ cb));
    }
    return diff == 0;
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::string> parseLogin(std::string_view input, std::string& error) {
    const std::string value = asciiLower(trim(input));
    if (!isLoginShape(value)) {
        error = "login must be 4-25 characters: letters, digits, underscore";
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> parseChannel(std::string_view input, std::string& error) {
    std::string value = trim(input);
    if (value.empty()) {
        error = "channel is empty";
        return std::nullopt;
    }

    const std::string lower = asciiLower(value);
    const std::string https = "https://";
    const std::string http = "http://";
    std::string_view rest = lower;
    if (lower.rfind(https, 0) == 0) {
        rest = std::string_view(lower).substr(https.size());
    } else if (lower.rfind(http, 0) == 0) {
        rest = std::string_view(lower).substr(http.size());
    } else if (lower.find("://") != std::string::npos) {
        error = "only https://twitch.tv/<channel> URLs are accepted";
        return std::nullopt;
    } else {
        if (!value.empty() && value[0] == '#') {
            value.erase(value.begin());
        }
        return parseLogin(value, error);
    }

    if (rest.rfind("www.", 0) == 0) {
        rest.remove_prefix(4);
    }
    const std::string host = "twitch.tv/";
    if (rest.rfind(host, 0) != 0) {
        error = "only twitch.tv channel URLs are accepted";
        return std::nullopt;
    }
    rest.remove_prefix(host.size());
    if (!rest.empty() && rest.back() == '/') {
        rest.remove_suffix(1);
    }
    if (rest.find('/') != std::string_view::npos || rest.find('?') != std::string_view::npos ||
        rest.find('#') != std::string_view::npos) {
        error = "channel URL must be https://twitch.tv/<channel> with no extra path";
        return std::nullopt;
    }
    return parseLogin(rest, error);
}

std::optional<std::string> normalizeToken(std::string_view input, std::string& error) {
    std::string value = trim(input);
    const std::string prefix = "oauth:";
    if (value.rfind(prefix, 0) == 0) {
        value.erase(0, prefix.size());
    }
    if (value.empty()) {
        error = "TWITCH_OAUTH_TOKEN is empty";
        return std::nullopt;
    }
    for (unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f) {
            error = "TWITCH_OAUTH_TOKEN contains whitespace or control characters";
            return std::nullopt;
        }
    }
    return value;
}

}  // namespace ircp
