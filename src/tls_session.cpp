#include "ircp/tls_session.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>

namespace ircp {
namespace {

struct OpenSslInit {
    OpenSslInit() { OPENSSL_init_ssl(0, nullptr); }
};

OpenSslInit& opensslInit() {
    static OpenSslInit init;
    return init;
}

std::string osslError() {
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        if (errno != 0) {
            return std::strerror(errno);
        }
        return "unknown tls error";
    }
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    return buf;
}

bool waitFd(int fd, short events, int timeout_ms, std::string& error) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    for (;;) {
        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc == 0) {
            error = "timeout";
            return false;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = std::string("poll failed: ") + std::strerror(errno);
            return false;
        }
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            error = "socket error";
            return false;
        }
        return true;
    }
}

bool setNonBlocking(int fd, std::string& error) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = std::string("fcntl failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

int connectOne(const addrinfo* info, int timeout_ms, std::string& error) {
    const int fd = ::socket(info->ai_family, info->ai_socktype | SOCK_CLOEXEC, info->ai_protocol);
    if (fd < 0) {
        error = std::string("socket failed: ") + std::strerror(errno);
        return -1;
    }
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (!setNonBlocking(fd, error)) {
        ::close(fd);
        return -1;
    }
    const int rc = ::connect(fd, info->ai_addr, info->ai_addrlen);
    if (rc == 0) {
        return fd;
    }
    if (errno != EINPROGRESS) {
        error = std::string("connect failed: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (!waitFd(fd, POLLOUT, timeout_ms, error)) {
        ::close(fd);
        return -1;
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
        error = std::string("connect failed: ") + std::strerror(so_error != 0 ? so_error : errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

bool parseStatus(const std::string& raw, int& status, std::string& body, std::string& error) {
    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error = "truncated http response";
        return false;
    }
    const std::string head = raw.substr(0, header_end);
    body = raw.substr(header_end + 4);
    std::istringstream stream(head);
    std::string version;
    stream >> version >> status;
    if (!stream || version.rfind("HTTP/", 0) != 0) {
        error = "malformed http status line";
        return false;
    }
    return true;
}

}  // namespace

struct TlsSession::Impl {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    int fd = -1;
    std::string buffer;
    bool open = false;
};

TlsSession::TlsSession() : impl_(std::make_unique<Impl>()) { (void)opensslInit(); }

TlsSession::~TlsSession() { close(); }

void TlsSession::close() {
    if (!impl_) {
        return;
    }
    impl_->open = false;
    if (impl_->ssl != nullptr) {
        SSL_free(impl_->ssl);
        impl_->ssl = nullptr;
    }
    if (impl_->ctx != nullptr) {
        SSL_CTX_free(impl_->ctx);
        impl_->ctx = nullptr;
    }
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
    impl_->buffer.clear();
}

bool TlsSession::isOpen() const { return impl_ && impl_->open; }

bool TlsSession::connect(const std::string& host, int port, std::string& error) {
    close();
    if (host.empty() || host.find('/') != std::string::npos || host.find(' ') != std::string::npos) {
        error = "refusing tls connection: invalid host";
        return false;
    }

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* result = nullptr;
    const std::string port_text = std::to_string(port);
    const int gai = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
    if (gai != 0) {
        error = std::string("dns failed: ") + gai_strerror(gai);
        return false;
    }

    int fd = -1;
    for (addrinfo* info = result; info != nullptr; info = info->ai_next) {
        fd = connectOne(info, 10000, error);
        if (fd >= 0) {
            break;
        }
    }
    ::freeaddrinfo(result);
    if (fd < 0) {
        if (error.empty()) {
            error = "connect failed";
        }
        return false;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == nullptr) {
        error = "tls context failed: " + osslError();
        ::close(fd);
        return false;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

    SSL* ssl = SSL_new(ctx);
    if (ssl == nullptr) {
        error = "tls setup failed: " + osslError();
        SSL_CTX_free(ctx);
        ::close(fd);
        return false;
    }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    // Hostname check. SSL_set1_host is deprecated in OpenSSL 4; the verify
    // param works on 3.x (Debian) and 4.x.
    X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
    if (param == nullptr || X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0) != 1) {
        error = "tls hostname check setup failed";
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(fd);
        return false;
    }
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

    for (;;) {
        const int rc = SSL_connect(ssl);
        if (rc == 1) {
            break;
        }
        const int ssl_error = SSL_get_error(ssl, rc);
        short events = 0;
        if (ssl_error == SSL_ERROR_WANT_READ) {
            events = POLLIN;
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
            events = POLLOUT;
        } else {
            error = "tls handshake failed: " + osslError();
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            ::close(fd);
            return false;
        }
        if (!waitFd(fd, events, 10000, error)) {
            error = "tls handshake failed: " + error;
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            ::close(fd);
            return false;
        }
    }

    const long verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK) {
        error = std::string("tls certificate verification failed: ") + X509_verify_cert_error_string(verify);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(fd);
        return false;
    }

    impl_->ctx = ctx;
    impl_->ssl = ssl;
    impl_->fd = fd;
    impl_->open = true;
    return true;
}

int TlsSession::readSome(std::string& chunk, int timeout_ms, std::string& error) {
    if (!impl_->open) {
        error = "not connected";
        return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (SSL_pending(impl_->ssl) == 0) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                                  .count();
            pollfd pfd{};
            pfd.fd = impl_->fd;
            pfd.events = POLLIN;
            const int rc = ::poll(&pfd, 1, static_cast<int>(left < 0 ? 0 : left));
            if (rc == 0) {
                return 0;
            }
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error = std::strerror(errno);
                impl_->open = false;
                return -1;
            }
        }
        char buf[4096];
        const int n = SSL_read(impl_->ssl, buf, sizeof(buf));
        if (n > 0) {
            chunk.append(buf, buf + n);
            return 1;
        }
        const int ssl_error = SSL_get_error(impl_->ssl, n);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            error = "connection closed";
            impl_->open = false;
            return -1;
        }
        error = "tls read failed: " + osslError();
        impl_->open = false;
        return -1;
    }
    return 0;
}

int TlsSession::readLine(std::string& line, int timeout_ms, std::string& error) {
    if (!impl_->open) {
        error = "not connected";
        return -1;
    }
    auto take = [&]() -> bool {
        std::size_t pos = impl_->buffer.find("\r\n");
        std::size_t extra = 2;
        if (pos == std::string::npos) {
            pos = impl_->buffer.find('\n');
            extra = 1;
        }
        if (pos == std::string::npos) {
            return false;
        }
        line = impl_->buffer.substr(0, pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        impl_->buffer.erase(0, pos + extra);
        return true;
    };
    if (take()) {
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        std::string chunk;
        const int rc = readSome(chunk, static_cast<int>(left < 1 ? 1 : left), error);
        if (rc == 0) {
            return 0;
        }
        if (rc < 0) {
            return -1;
        }
        if (impl_->buffer.size() + chunk.size() > 16384 && chunk.find('\n') == std::string::npos &&
            impl_->buffer.find('\n') == std::string::npos) {
            error = "irc line exceeds 16KiB";
            impl_->open = false;
            return -1;
        }
        impl_->buffer += chunk;
        if (take()) {
            return 1;
        }
    }
    return 0;
}

bool TlsSession::writeRaw(const std::string& data, std::string& error) {
    if (!impl_->open) {
        error = "not connected";
        return false;
    }
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = SSL_write(impl_->ssl, data.data() + sent, static_cast<int>(data.size() - sent));
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        const int ssl_error = SSL_get_error(impl_->ssl, n);
        short events = 0;
        if (ssl_error == SSL_ERROR_WANT_READ) {
            events = POLLIN;
        } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
            events = POLLOUT;
        } else {
            error = "tls write failed: " + osslError();
            impl_->open = false;
            return false;
        }
        if (!waitFd(impl_->fd, events, 5000, error)) {
            impl_->open = false;
            return false;
        }
    }
    return true;
}

bool TlsSession::writeLine(const std::string& line, std::string& error) {
    if (line.find('\r') != std::string::npos || line.find('\n') != std::string::npos) {
        error = "refusing to write an irc line that already contains a newline";
        return false;
    }
    return writeRaw(line + "\r\n", error);
}

bool TlsSession::httpsGet(const std::string& host, const std::string& path,
                          const std::vector<std::pair<std::string, std::string>>& headers, int& status,
                          std::string& body, std::string& error) {
    if (path.empty() || path[0] != '/' || path.find(' ') != std::string::npos) {
        error = "refusing https request: invalid path";
        return false;
    }
    TlsSession session;
    if (!session.connect(host, 443, error)) {
        return false;
    }
    std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                          "\r\nUser-Agent: irc-parser/1.1 (self-hosted; read-only)\r\nAccept: application/json\r\n"
                          "Connection: close\r\n";
    for (const auto& header : headers) {
        if (header.first.find('\r') != std::string::npos || header.first.find('\n') != std::string::npos ||
            header.second.find('\r') != std::string::npos || header.second.find('\n') != std::string::npos) {
            error = "refusing https request: header contains a newline";
            return false;
        }
        request += header.first + ": " + header.second + "\r\n";
    }
    request += "\r\n";
    if (!session.writeRaw(request, error)) {
        return false;
    }

    std::string raw;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline && raw.size() < 65536) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        std::string chunk;
        const int rc = session.readSome(chunk, static_cast<int>(left < 1 ? 1 : left), error);
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            break;
        }
        raw += chunk;
    }
    if (raw.empty()) {
        if (error.empty()) {
            error = "empty https response";
        }
        return false;
    }
    if (raw.size() >= 65536) {
        error = "https response too large";
        return false;
    }
    return parseStatus(raw, status, body, error);
}

}  // namespace ircp
