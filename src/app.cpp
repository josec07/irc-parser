#include "ircp/app.h"

#include "ircp/demo.h"
#include "ircp/logger.h"
#include "ircp/util.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

namespace ircp {
namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop = true; }

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

void jsonError(HttpResponse& response, int status, const std::string& message) {
    response.status = status;
    response.body = nlohmann::json{{"error", message}}.dump();
}

}  // namespace

App::App(Config config)
    : config_(std::move(config)),
      analytics_(config_.windowSeconds()),
      client_(config_, analytics_),
      server_(config_.bindAddress(), config_.port(), config_.apiKey(),
              [this](const HttpRequest& request, HttpResponse& response) { onRequest(request, response); }) {}

App::~App() = default;

int App::run() {
    g_stop = false;
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    client_.start();
    try {
        server_.start();
    } catch (const std::exception& ex) {
        Logger::error(ex.what());
        client_.stop();
        return 1;
    }

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    Logger::info("shutting down");
    server_.stop();
    client_.stop();
    return 0;
}

void App::onRequest(const HttpRequest& request, HttpResponse& response) {
    if (request.method == "GET" && request.path == "/demo") {
        response.status = 200;
        response.content_type = "text/html; charset=utf-8";
        response.body = demoPageHtml();
        return;
    }
    if (request.method == "GET" && request.path == "/demo/snapshot") {
        response.status = 200;
        response.body = demoSnapshot(client_.statusJson(), analytics_.listJson()).dump();
        return;
    }
    if (request.method == "GET" && request.path == "/v1/status") {
        response.status = 200;
        response.body = client_.statusJson().dump();
        return;
    }
    if (request.method == "GET" && request.path == "/v1/channels") {
        response.status = 200;
        response.body = analytics_.listJson().dump();
        return;
    }

    const std::vector<std::string> parts = splitPath(request.path);
    // "", "v1", "channels", "{name}", optional "analytics"
    if (parts.size() >= 3 && parts[1] == "v1" && parts[2] == "channels") {
        if (parts.size() == 3 && request.method == "POST") {
            const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
            if (body.is_discarded() || !body.is_object() || !body.contains("channel") || !body["channel"].is_string()) {
                jsonError(response, 400, "expected {\"channel\":\"name-or-twitch-url\"}");
                return;
            }
            std::string error;
            const auto channel = parseChannel(body["channel"].get<std::string>(), error);
            if (!channel) {
                jsonError(response, 400, error);
                return;
            }
            const WatchStatus status = client_.watch(*channel);
            if (status == WatchStatus::Duplicate) {
                jsonError(response, 409, "already watching");
                return;
            }
            if (status == WatchStatus::AtCap) {
                jsonError(response, 403, "channel cap reached");
                return;
            }
            if (status == WatchStatus::Invalid) {
                jsonError(response, 400, "invalid channel");
                return;
            }
            response.status = status == WatchStatus::Created ? 201 : 200;
            response.body = analytics_.snapshotJson(*channel).dump();
            return;
        }

        if (parts.size() == 4 || (parts.size() == 5 && parts[4] == "analytics")) {
            std::string error;
            const auto channel = parseChannel(parts[3], error);
            if (!channel) {
                jsonError(response, 400, error);
                return;
            }
            if (parts.size() == 4 && request.method == "DELETE") {
                if (!client_.unwatch(*channel)) {
                    jsonError(response, 404, "channel is not being watched");
                    return;
                }
                response.status = 204;
                response.body.clear();
                return;
            }
            if (request.method == "GET" && (parts.size() == 5 || parts.size() == 4)) {
                const auto snapshot = analytics_.snapshot(*channel);
                if (!snapshot) {
                    jsonError(response, 404, "channel is not being watched");
                    return;
                }
                response.status = 200;
                response.body = snapshotJson(*snapshot, true).dump();
                return;
            }
            jsonError(response, 405, "method not allowed");
            return;
        }
    }

    jsonError(response, 404, "not found");
}

}  // namespace ircp
