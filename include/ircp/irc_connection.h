#ifndef IRCP_CONNECTION_H
#define IRCP_CONNECTION_H

#include <string>
#include <boost/asio.hpp>

namespace ircp {

class IrcConnection {
public:
    IrcConnection(const std::string& host, int port);
    bool connect();
    void run();
    std::string readLine();
    void send(const std::string& message);
    void sendPassword(const std::string& password);
    void sendJoin(const std::string& channel);
    void disconnect();
    bool isConnected() const { return connected_; }

private:
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::streambuf buffer_;
    bool connected_;

    void startRead();
    void handleLine(const std::string& line);
};

}

#endif
