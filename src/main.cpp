#include "ircp/ircp.h"
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
        std::cerr << "Invalid channel or URL provided." << std::endl;
        return 1;
    }

    // Default connection details for anon read-only (plain TCP IRC is port 6667)
    std::string host = "irc.twitch.tv";
    int port = 6667;
    std::string password = "SCHMOOPIIE";

    TwitchIRC irc(host, port, password, channel);
    
    if (!irc.connectToTwitch()) {
        std::cerr << "Failed to connect to Twitch IRC." << std::endl;
        return 1;
    }

    std::deque<std::string> lastN;
    const size_t MAX = 1000;
    //one line
    while (true) {
        std::string line = irc.readLine();
        if (line.empty())continue;
        
        std::string json_msg = irc.parse_message(line);
        if (json_msg.empty())continue;
        
        lastN.push_back(json_msg);
        std::cout << json_msg << std::endl;
        if(lastN.size() > MAX)lastN.pop_front();
    }

    return 0;
}
