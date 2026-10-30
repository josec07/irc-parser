#include "ircp/twitch_irc.h"
#include "ircp/logger.h"
#include "ircp/url.h"
#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

TwitchIRC::TwitchIRC(const std::string &host, int port,
                     const std::string &password, const std::string &channel)
    : irc_session_(host, port), password_(password), channel_(channel),
      twitch_connected_(false), message_count_(0) {
  // Initialization
}

void TwitchIRC::run() { irc_session_.run(); }

bool TwitchIRC::connectToTwitch() {
  disconnectFromTwitch();

  if (!irc_session_.openSocket()) {
    return false;
  }

  sendPassword(password_);

  // send NICK and USER
  std::string nick = "justinfan" + std::to_string(rand() % 100000);

  irc_session_.send("NICK " + nick + "\r\n");

  irc_session_.send("USER " + nick + " 0 * :" + nick + "\r\n");

  // Wait for welcome message

  auto start = std::chrono::steady_clock::now();
  bool got_welcome = false;
  int line_count = 0;

  while (!got_welcome) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

    if (elapsed > 10) {
      Logger::info("[No Welcome Timeout] [TwitchIRC] Elapsed time: " +
                   std::to_string(elapsed) +
                   "\n[TwitchIRC] Lines received before timeout: " +
                   std::to_string(line_count));
      return false;
    }

    std::string line = readLine();
    if (line.empty()) {
      if (!irc_session_.isSocketOpen()) {
        std::cerr << "[TwitchIRC] Socket closed while waiting for welcome"
                  << std::endl;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    line_count++;
    std::cout << "[TwitchIRC] <-- Line #" << line_count << " (t=" << elapsed
              << "s): " << line << std::endl;

    if (line.find("Welcome") != std::string::npos) {
      std::cout << "[TwitchIRC] Welcome message found! (contains 'Welcome')"
                << std::endl;
      got_welcome = true;
    }
  }

  // Join the channel
  std::cout << "[TwitchIRC] Step 7: Joining channel #" << channel_ << "..."
            << std::endl;
  sendJoin(channel_);
  std::cout << "[TwitchIRC] Step 7: JOIN command sent" << std::endl;

  twitch_connected_ = true;
  std::cout
      << "[TwitchIRC] === Twitch IRC session established! Now monitoring #"
      << channel_ << " ===" << std::endl;
  return true;
}

bool TwitchIRC::reconnectToTwitch() {
  disconnectFromTwitch();

  for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
    Logger::info("[TwitchIRC] Reconnect attempt " + std::to_string(attempt) +
                 " of " + std::to_string(MAX_RETRIES) + "...");
    std::cout << "[TwitchIRC] Reconnect attempt " << attempt << " of "
              << MAX_RETRIES << "..." << std::endl;

    if (connectToTwitch()) {
      Logger::info("[TwitchIRC] Reconnected to Twitch IRC successfully.");
      return true;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
  }

  Logger::error("[TwitchIRC] Failed to reconnect after " +
                std::to_string(MAX_RETRIES) + " attempts.");
  std::cerr << "[TwitchIRC] Failed to reconnect after " << MAX_RETRIES
            << " attempts." << std::endl;
  return false;
}

std::string TwitchIRC::readLine() {
  std::string line = irc_session_.readLine();
  if (!line.empty()) {
    handlePing(line);
    return line;
  }

  if (twitch_connected_ && !irc_session_.isSocketOpen()) {
    Logger::warn("[TwitchIRC] Socket disconnected. Reconnecting to Twitch...");
    reconnectToTwitch();
  }

  return "";
}

bool TwitchIRC::handlePing(const std::string &line) {
  if (line.rfind("PING", 0) != 0) {
    return false;
  }

  std::string payload = (line.length() > 5) ? line.substr(5) : ":tmi.twitch.tv";
  std::cout << "[TwitchIRC] PING received, replying with PONG " << payload
            << std::endl;
  sendPong(payload);
  return true;
}

void TwitchIRC::sendPassword(const std::string &password) {
  irc_session_.send("PASS " + password + "\r\n");
}

void TwitchIRC::sendJoin(const std::string &channel) {
  irc_session_.send("JOIN #" + channel + "\r\n");
}

void TwitchIRC::sendPong(const std::string &payload) {
  irc_session_.send("PONG " + payload + "\r\n");
}

std::string TwitchIRC::extract_twitch_channel_from_url(const std::string &url) {
  return extract_channel_from_url(url);
}

std::string TwitchIRC::parse_message(const std::string &raw) {
  // Example message: ":justin!justin@justin.tmi.twitch.tv PRIVMSG #justin
  // :Hello, world!" Find the first space to separate prefix from command
  size_t first_space = raw.find(' ');
  if (first_space == std::string::npos)
    return "";

  std::string prefix = raw.substr(0, first_space);

  // Find the command (PRIVMSG)
  size_t second_space = raw.find(' ', first_space + 1);
  if (second_space == std::string::npos)
    return "";

  std::string command =
      raw.substr(first_space + 1, second_space - first_space - 1);

  if (command != "PRIVMSG")
    return "";

  // Find the channel parameter
  size_t third_space = raw.find(' ', second_space + 1);
  if (third_space == std::string::npos)
    return "";

  std::string channel =
      raw.substr(second_space + 1, third_space - second_space - 1);
  // Remove '#' if present
  if (!channel.empty() && channel[0] == '#') {
    channel = channel.substr(1);
  }

  // Find the message content (starts with :)
  size_t msg_start = raw.find(':', third_space + 1);
  if (msg_start == std::string::npos)
    return "";

  // Extract username from prefix (remove leading : and extract before !)
  size_t exclamation = prefix.find('!');
  if (exclamation == std::string::npos || exclamation == 0)
    return "";
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

void TwitchIRC::disconnectFromTwitch() {
  irc_session_.closeSocket();
  twitch_connected_ = false;
}

bool TwitchIRC::test_connection(const std::string &channel,
                                int timeout_seconds) {
  if (!connectToTwitch()) {
    return false;
  }

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::steady_clock::now() - start)
             .count() < timeout_seconds) {
    std::string line = readLine();
    if (!line.empty()) {
      std::string json_msg = parse_message(line);
      if (!json_msg.empty()) {
        disconnectFromTwitch();
        return true;
      }
    }
  }

  disconnectFromTwitch();
  return true;
}
