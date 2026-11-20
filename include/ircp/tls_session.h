#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ircp {

// One TLS connection. The IRC host is not configurable: the OAuth token is
// only ever sent to irc.chat.twitch.tv after certificate verification.
class TlsSession {
public:
    TlsSession();
    ~TlsSession();

    TlsSession(const TlsSession&) = delete;
    TlsSession& operator=(const TlsSession&) = delete;

    bool connect(const std::string& host, int port, std::string& error);
    void close();
    bool isOpen() const;

    // 1 = a line, 0 = timeout, -1 = closed or error. The line has no CRLF.
    int readLine(std::string& line, int timeout_ms, std::string& error);
    bool writeLine(const std::string& line, std::string& error);

    // One HTTPS GET. Does not follow redirects. Authorization is not logged.
    static bool httpsGet(const std::string& host, const std::string& path,
                         const std::vector<std::pair<std::string, std::string>>& headers, int& status,
                         std::string& body, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool writeRaw(const std::string& data, std::string& error);
    int readSome(std::string& chunk, int timeout_ms, std::string& error);
};

}  // namespace ircp
