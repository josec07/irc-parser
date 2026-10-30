#include "ircp/twitch_irc.h"
#include "ircp/url.h"
#include <iostream>
#include <string>
#include "ircp/logger.h"

int main(int argc, char* argv[]) {

    Logger::initialize("file_logger", "logs/irc.log");
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <twitch_channel_or_url>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string channel = parse_url_or_channel(input);

    if (channel.empty()) {
        Logger::error("[Main] Empty Channel Name.");
        return 1;
    }

    // Default connection details for anon read-only (plain TCP IRC is port 6667)
    std::string host = "irc.twitch.tv";
    int port = 6667;
    std::string password = "SCHMOOPIIE";

    TwitchIRC irc(host, port, password, channel);
    
    if (!irc.connectToTwitch()) {
        Logger::error("Failed to connect to Twitch IRC.");
        return 1;
    }


    return 0;
}
