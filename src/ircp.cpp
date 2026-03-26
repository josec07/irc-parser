#include "ircp/ircp.h"
#include <iostream>
#include <boost/asio.hpp>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

namespace ircp {

TwitchIRC::TwitchIRC(const std::string& host, int port, const std::string& password, const std::string& channel)
    : irc_connection_(host, port), password_(password), channel_(channel), connected_(false), message_count_(0) {
    // Initialization
}

void TwitchIRC::run() {
    irc_connection_.run();
}

bool TwitchIRC::connect() {
    // Suppressed stdout logging
    
    // First, establish TCP connection
    if (!irc_connection_.connect()) {
        std::cerr << "[TwitchIRC] TCP connection failed" << std::endl;
        return false;
    }
    
    // send PASS
    irc_connection_.sendPassword(password_);

    // send NICK and USER
    std::string nick = "justin" + std::to_string(rand() % 1000);
    irc_connection_.send("NICK " + nick + "\r\n");
    irc_connection_.send("USER " + nick + " 0 * :" + nick + "\r\n");

    // Wait for welcome message
    auto start = std::chrono::steady_clock::now();
    bool got_welcome = false;

    while (!got_welcome) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed > 10) {
            std::cerr << "[TwitchIRC] Timeout waiting for welcome message" << std::endl;
            return false;
        }

        std::string line = irc_connection_.readLine();
        if (line.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        if (line.find("Welcome") != std::string::npos) {
            got_welcome = true;
        }
    }

    // Join the channel
    irc_connection_.send("JOIN #" + channel_ + "\r\n");
    
    connected_ = true;
    return true;
}

std::string TwitchIRC::readLine() {
    return irc_connection_.readLine();
}

std::string TwitchIRC::extract_twitch_channel_from_url(const std::string& url) {
    // Remove protocol and www
    std::string cleaned = url;
    std::replace(cleaned.begin(), cleaned.end(), ':', ' ');
    std::replace(cleaned.begin(), cleaned.end(), '/', ' ');
    std::istringstream iss(cleaned);
    std::string part;

    // Skip "www.twitch.tv"
    while (iss >> part) {
        if (part == "www.twitch.tv") {
            if (iss >> part) {
                return part; // Return the channel name
            }
        }
    }

    return ""; // Return empty string if parsing failed
}

std::string TwitchIRC::parse_message(const std::string& raw) {
    // Example message: ":justin!justin@justin.tmi.twitch.tv PRIVMSG #justin :Hello, world!"
    // Find the first space to separate prefix from command
    size_t first_space = raw.find(' ');
    if (first_space == std::string::npos) return "";
    
    std::string prefix = raw.substr(0, first_space);
    
    // Find the command (PRIVMSG)
    size_t second_space = raw.find(' ', first_space + 1);
    if (second_space == std::string::npos) return "";
    
    std::string command = raw.substr(first_space + 1, second_space - first_space - 1);
    
    if (command != "PRIVMSG") return "";
    
    // Find the channel parameter
    size_t third_space = raw.find(' ', second_space + 1);
    if (third_space == std::string::npos) return "";
    
    std::string channel = raw.substr(second_space + 1, third_space - second_space - 1);
    // Remove '#' if present
    if (!channel.empty() && channel[0] == '#') {
        channel = channel.substr(1);
    }
    
    // Find the message content (starts with :)
    size_t msg_start = raw.find(':', third_space + 1);
    if (msg_start == std::string::npos) return "";
    
    // Extract username from prefix (remove leading : and extract before !)
    size_t exclamation = prefix.find('!');
    if (exclamation == std::string::npos || exclamation == 0) return "";
    std::string username = prefix.substr(1, exclamation - 1); // Remove leading :
    
    // Extract message content
    std::string content = raw.substr(msg_start + 1);
    
    // Format to JSON
    nlohmann::json j;
    j["channel"] = channel;
    j["username"] = username;
    j["message"] = content;
    
    return j.dump();
}

void TwitchIRC::disconnect() {
    irc_connection_.disconnect();
    connected_ = false;
}

bool TwitchIRC::test_connection(const std::string& channel, int timeout_seconds) {
    if (!connect()) {
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() < timeout_seconds) {
        std::string line = readLine();
        if (!line.empty()) {
            std::string json_msg = parse_message(line);
            if (!json_msg.empty()) {
                disconnect();
                return true;
            }
        }
    }

    disconnect();
    return true;
}

}
