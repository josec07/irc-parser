#pragma once

#include "ircp/analytics.h"
#include "ircp/config.h"
#include "ircp/http_server.h"
#include "ircp/twitch_client.h"

namespace ircp {

class App {
public:
    explicit App(Config config);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();

private:
    void onRequest(const HttpRequest& request, HttpResponse& response);

    Config config_;
    AnalyticsStore analytics_;
    TwitchClient client_;
    HttpServer server_;
};

}  // namespace ircp
