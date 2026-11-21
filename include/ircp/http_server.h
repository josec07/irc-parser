#pragma once

#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

namespace ircp {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct HttpResponse {
    int status = 200;
    std::string body;
    std::string content_type = "application/json";
};

bool parseHttpRequest(const std::string& raw, HttpRequest& out, std::string& error);
std::string serializeHttpResponse(const HttpResponse& response);
bool requestAuthorized(const HttpRequest& request, const std::string& api_key);

class HttpServer {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    HttpServer(std::string bind_address, int port, std::string api_key, Handler handler);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();

private:
    void acceptLoop();
    void handleClient(int fd);

    std::string bind_address_;
    int port_;
    std::string api_key_;
    Handler handler_;
    int listen_fd_ = -1;
    std::thread thread_;
    bool started_ = false;
};

}  // namespace ircp
