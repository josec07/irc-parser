#include "ircp/ircp.h"
#include "ircp/url.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <twitch_channel_or_url>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string channel = ircp::parse_url_or_channel(input);

    if (channel.empty()) {
        std::cerr << "Invalid channel or URL provided." << std::endl;
        return 1;
    }

    // Default connection details for anon read-only
    std::string host = "irc.twitch.tv";
    int port = 6667;
    std::string password = "SCHMOOPIIE";

    ircp::TwitchIRC irc(host, port, password, channel);

    if (!irc.connect()) {
        std::cerr << "Failed to connect to Twitch IRC." << std::endl;
        return 1;
    }

    while (true) {
        std::string line = irc.readLine();
        if (!line.empty()) {
            std::string json_msg = irc.parse_message(line);
            if (!json_msg.empty()) {
                std::cout << json_msg << std::endl;
            }
        }
    }

    return 0;
}
