#include "ircp/http_server.h"

#include "ircp/logger.h"
#include "ircp/util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ircp {
namespace {

std::string statusText(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 409:
            return "Conflict";
        default:
            return "Error";
    }
}

std::string headerValue(const HttpRequest& request, const std::string& name) {
    const auto it = request.headers.find(name);
    if (it == request.headers.end()) {
        return {};
    }
    return it->second;
}

std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            const int value = std::stoi(in.substr(i + 1, 2), nullptr, 16);
            out.push_back(static_cast<char>(value));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::size_t contentLength(const std::string& headers) {
    const std::string lower = asciiLower(headers);
    const std::string key = "content-length:";
    const std::size_t pos = lower.find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    std::size_t i = pos + key.size();
    while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t')) {
        ++i;
    }
    std::size_t end = i;
    while (end < headers.size() && headers[end] >= '0' && headers[end] <= '9') {
        ++end;
    }
    if (end == i) {
        return static_cast<std::size_t>(-1);
    }
    try {
        return static_cast<std::size_t>(std::stoul(headers.substr(i, end - i)));
    } catch (const std::exception&) {
        return static_cast<std::size_t>(-1);
    }
}

bool readUntil(int fd, std::string& data, const std::string& marker, std::size_t limit, std::string& error) {
    char buf[1024];
    while (data.find(marker) == std::string::npos) {
        if (data.size() >= limit) {
            error = "request too large";
            return false;
        }
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            error = "client closed";
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::strerror(errno);
            return false;
        }
        data.append(buf, buf + n);
    }
    return true;
}

}  // namespace

bool parseHttpRequest(const std::string& raw, HttpRequest& out, std::string& error) {
    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error = "incomplete http headers";
        return false;
    }
    std::istringstream stream(raw.substr(0, header_end));
    std::string target;
    std::string version;
    stream >> out.method >> target >> version;
    if (!stream || (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        error = "malformed request line";
        return false;
    }
    const std::size_t query = target.find('?');
    if (query != std::string::npos) {
        // Query strings are ignored. The API key must not be accepted from the URL.
        target = target.substr(0, query);
    }
    out.path = urlDecode(target);
    if (out.path.find("..") != std::string::npos || out.path.find('\\') != std::string::npos ||
        out.path.find('\0') != std::string::npos) {
        error = "invalid path";
        return false;
    }

    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = asciiLower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        out.headers.emplace(std::move(name), std::move(value));
    }

    const std::string length_text = headerValue(out, "content-length");
    std::size_t length = 0;
    if (!length_text.empty()) {
        try {
            length = static_cast<std::size_t>(std::stoul(length_text));
        } catch (const std::exception&) {
            error = "invalid content-length";
            return false;
        }
    }
    if (length > 4096) {
        error = "body too large";
        return false;
    }
    const std::string rest = raw.substr(header_end + 4);
    if (rest.size() < length) {
        error = "incomplete body";
        return false;
    }
    out.body = rest.substr(0, length);
    return true;
}

std::string serializeHttpResponse(const HttpResponse& response) {
    const std::string text = statusText(response.status);
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << text << "\r\n"
        << "Content-Type: " << response.content_type << "\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n\r\n"
        << response.body;
    return out.str();
}

bool requestAuthorized(const HttpRequest& request, const std::string& api_key) {
    const std::string authorization = headerValue(request, "authorization");
    if (authorization.size() >= 7 && asciiLower(authorization.substr(0, 7)) == "bearer ") {
        if (constantTimeEqual(trim(authorization.substr(7)), api_key)) {
            return true;
        }
    }
    const std::string key = headerValue(request, "x-api-key");
    return !key.empty() && constantTimeEqual(key, api_key);
}

HttpServer::HttpServer(std::string bind_address, int port, std::string api_key, Handler handler)
    : bind_address_(std::move(bind_address)),
      port_(port),
      api_key_(std::move(api_key)),
      handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
    if (started_) {
        return;
    }
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }
    const int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr) != 1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("IRCP_BIND is not an IPv4 address");
    }
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const std::string message = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("bind failed: " + message);
    }
    if (::listen(listen_fd_, 16) < 0) {
        const std::string message = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("listen failed: " + message);
    }
    started_ = true;
    thread_ = std::thread([this] { acceptLoop(); });
    Logger::info("api listening on " + bind_address_ + ":" + std::to_string(port_));
}

void HttpServer::stop() {
    started_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HttpServer::acceptLoop() {
    while (started_) {
        const int client = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (!started_ || errno == EBADF || errno == EINVAL) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            continue;
        }
        handleClient(client);
        ::close(client);
    }
}

void HttpServer::handleClient(int fd) {
    timeval tv{};
    tv.tv_sec = 5;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    HttpResponse response;
    std::string raw;
    std::string error;
    HttpRequest request;
    bool parsed = false;
    if (!readUntil(fd, raw, "\r\n\r\n", 8192, error)) {
        response.status = 400;
        response.body = "{\"error\":\"bad request\"}";
    } else {
        const std::size_t header_end = raw.find("\r\n\r\n");
        const std::size_t length = contentLength(raw.substr(0, header_end));
        if (length != static_cast<std::size_t>(-1) && length <= 4096) {
            while (raw.size() < header_end + 4 + length && raw.size() < 8192 + length) {
                char buf[1024];
                const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    break;
                }
                raw.append(buf, buf + n);
            }
        }
        if (length > 4096 || !parseHttpRequest(raw, request, error)) {
            response.status = 400;
            response.body = "{\"error\":\"bad request\"}";
        } else {
            parsed = true;
            const bool public_demo = request.method == "GET" &&
                                     (request.path == "/demo" || request.path == "/demo/snapshot");
            if (request.method == "GET" && request.path == "/health") {
                response.status = 200;
                response.body = "{\"status\":\"ok\"}";
            } else if (!public_demo && !requestAuthorized(request, api_key_)) {
                response.status = 401;
                response.body = "{\"error\":\"unauthorized\"}";
            } else {
                try {
                    handler_(request, response);
                } catch (const std::exception& ex) {
                    Logger::error(std::string("request failed: ") + ex.what());
                    response.status = 500;
                    response.body = "{\"error\":\"internal error\"}";
                }
            }
        }
    }
    if (parsed) {
        Logger::info(request.method + " " + request.path + " -> " + std::to_string(response.status));
    }

    const std::string bytes = serializeHttpResponse(response);
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        sent += static_cast<std::size_t>(n);
    }
}

}  // namespace ircp
