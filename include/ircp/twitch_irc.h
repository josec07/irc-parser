#ifndef IRCP_H
#define IRCP_H

#include "ircp/session.h"
#include <string>


class TwitchIRC {
public:
    TwitchIRC(const std::string& host, int port, const std::string& password, const std::string& channel);
    void run();
    bool connectToTwitch();
    bool reconnectToTwitch();
    std::string readLine();
    bool handlePing(const std::string& line);
    void sendPassword(const std::string& password);
    void sendJoin(const std::string& channel);
    void sendPong(const std::string& payload);
    std::string extract_twitch_channel_from_url(const std::string& url);
    // Modified to output json string, returns empty string if not a chat message
    std::string parse_message(const std::string& raw);
    void disconnectFromTwitch();
    bool test_connection(const std::string& channel, int timeout_seconds);

private:
    Session irc_session_;
    std::string password_;
    std::string channel_;
    bool twitch_connected_;
    int message_count_;
    static constexpr int MAX_RETRIES = 5;
};

#endif
