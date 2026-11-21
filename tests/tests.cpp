#include "ircp/analytics.h"
#include "ircp/demo.h"
#include "ircp/compliance.h"
#include "ircp/config.h"
#include "ircp/http_server.h"
#include "ircp/irc_message.h"
#include "ircp/twitch_client.h"
#include "ircp/util.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

void check(bool condition, const char* text, int line) {
    if (!condition) {
        std::cerr << "FAIL " << line << ": " << text << '\n';
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

ircp::IrcMessage privmsg(const std::string& raw) {
    auto parsed = ircp::IrcParser::parse(raw);
    CHECK(parsed.has_value());
    return parsed.value_or(ircp::IrcMessage{});
}

}  // namespace

int main() {
    using namespace ircp;

    static_assert(Compliance::kReadOnly);
    static_assert(!Compliance::kPersistsChat);
    static_assert(!Compliance::kRetainsMessageText);
    static_assert(!Compliance::kRequestsMembership);
    static_assert(Compliance::kUsesTls);
    static_assert(!Compliance::kHostConfigurable);
    static_assert(Compliance::kHardMaxChannels <= 100);
    static_assert(Compliance::kHardMaxWindowSeconds <= 86400);
    static_assert(std::string_view(Compliance::kIrcHost) == "irc.chat.twitch.tv");
    static_assert(Compliance::kIrcPort == 6697);

    {
        auto parsed = IrcParser::parse(
            "@badge-info=;badges=broadcaster/1;display-name=Foo\\sBar;emotes=25:0-4,12-16/1902:6-10;first-msg=0;"
            "id=abc;mod=0;subscriber=1;bits=100 :ronni!ronni@ronni.tmi.twitch.tv PRIVMSG #Dallas :Kappa Keepo Kappa");
        CHECK(parsed.has_value());
        CHECK(parsed->command == "PRIVMSG");
        CHECK(parsed->nick == "ronni");
        CHECK(parsed->tag("display-name") == "Foo Bar");
        CHECK(parsed->tag("bits") == "100");
        CHECK(parsed->has_trailing);
        CHECK(parsed->trailing == "Kappa Keepo Kappa");
        CHECK(channelParam(parsed->params[0]) == "dallas");
    }

    {
        auto ping = IrcParser::parse("PING :tmi.twitch.tv");
        CHECK(ping.has_value());
        CHECK(ping->command == "PING");
        CHECK(ping->trailing == "tmi.twitch.tv");
        CHECK(pongPayload(*ping) == ":tmi.twitch.tv");
    }

    {
        IrcMessage nasty;
        nasty.command = "PING";
        nasty.has_trailing = true;
        nasty.trailing = "tmi.twitch.tv\r\nPRIVMSG #x :hi";
        CHECK(pongPayload(nasty) == ":tmi.twitch.tv");
    }

    CHECK(IrcParser::unescapeTag("a\\:b\\s\\\\") == "a;b \\");
    CHECK(!IrcParser::parse("").has_value());

    std::string error;
    CHECK(parseChannel("https://www.twitch.tv/SomeChannel", error) == "somechannel");
    CHECK(parseChannel("#Dallas", error) == "dallas");
    CHECK(!parseChannel("https://evil.example/twitch.tv/foo", error).has_value());
    CHECK(!parseChannel("https://twitch.tv.evil/foo", error).has_value());
    CHECK(!parseChannel("ab", error).has_value());
    CHECK(!parseChannel("has space", error).has_value());
    CHECK(!normalizeToken("oauth:bad\ntoken", error).has_value());
    CHECK(normalizeToken("oauth:abc123", error) == "abc123");

    std::int64_t now = 1'000'000;
    AnalyticsStore store(60, [&] { return now; });
    store.ensureChannel("dallas");

    store.ingest(privmsg(
        "@badges=broadcaster/1;bits=50;emotes=25:0-4;first-msg=1;id=m1;mod=1;subscriber=1;vip=1 "
        ":ronni!ronni@ronni.tmi.twitch.tv PRIVMSG #dallas :!Song hello"));
    store.ingest(privmsg(
        "@badges=;id=m2;source-id=shared;subscriber=0 :other!other@other.tmi.twitch.tv PRIVMSG #dallas :hi"));
    store.ingest(privmsg(
        "@badges=;id=m3;source-id=shared :other!other@other.tmi.twitch.tv PRIVMSG #dallas :hi again"));

    auto snap = store.snapshot("dallas");
    CHECK(snap.has_value());
    CHECK(snap->messages == 2);
    CHECK(snap->unique_chatters == 2);
    CHECK(snap->first_messages == 1);
    CHECK(snap->subscriber_messages == 1);
    CHECK(snap->moderator_messages == 1);
    CHECK(snap->vip_messages == 1);
    CHECK(snap->broadcaster_messages == 1);
    CHECK(snap->bits == 50);
    CHECK(snap->commands == 1);
    CHECK(snap->top_commands.size() == 1);
    CHECK(snap->top_commands[0].name == "!song");
    CHECK(snap->top_emotes.size() == 1);
    CHECK(snap->messages_per_minute == 2.0);

    auto json = snapshotJson(*snap, true);
    const std::set<std::string> allowed = {
        "channel", "join_state", "block_reason", "window_seconds", "generated_at", "messages",
        "messages_per_minute", "unique_chatters", "first_messages", "subscriber_messages",
        "moderator_messages", "vip_messages", "broadcaster_messages", "bits", "commands",
        "subscriptions", "resubscriptions", "gifted_subscriptions", "raids", "timeouts", "bans",
        "chat_clears", "other_notices", "room", "top_chatters", "top_commands", "top_emotes"};
    for (auto it = json.begin(); it != json.end(); ++it) {
        CHECK(allowed.count(it.key()) == 1);
    }
    CHECK(json.dump().find("hello") == std::string::npos);
    CHECK(!json.contains("message"));
    CHECK(json.dump().find("\"message\"") == std::string::npos);
    CHECK(!json.contains("user_id"));
    CHECK(!json.contains("user-id"));

    store.ingest(privmsg("@msg-id=raid :tmi.twitch.tv USERNOTICE #dallas"));
    store.ingest(privmsg("@msg-id=sub :tmi.twitch.tv USERNOTICE #dallas"));
    snap = store.snapshot("dallas");
    CHECK(snap->raids == 1);
    CHECK(snap->subscriptions == 1);
    CHECK(snap->messages == 2);

    store.ingest(privmsg("@target-msg-id=m1 :tmi.twitch.tv CLEARMSG #dallas :!Song hello"));
    snap = store.snapshot("dallas");
    CHECK(snap->messages == 1);
    CHECK(snap->bits == 0);

    store.ingest(privmsg("@ban-duration=30 :tmi.twitch.tv CLEARCHAT #dallas :other"));
    snap = store.snapshot("dallas");
    CHECK(snap->messages == 0);
    CHECK(snap->timeouts == 1);

    store.ingest(privmsg(":someone!someone@someone.tmi.twitch.tv PRIVMSG #dallas :still here"));
    store.ingest(privmsg(":tmi.twitch.tv CLEARCHAT #dallas"));
    snap = store.snapshot("dallas");
    CHECK(snap->messages == 0);
    CHECK(snap->chat_clears == 1);

    IrcMessage room = privmsg("@emote-only=1;slow=5;room-id=42 :tmi.twitch.tv ROOMSTATE #dallas");
    store.ingest(room);
    room = privmsg("@r9k=1 :tmi.twitch.tv ROOMSTATE #dallas");
    store.ingest(room);
    snap = store.snapshot("dallas");
    CHECK(snap->emote_only == 1);
    CHECK(snap->slow_seconds == 5);
    CHECK(snap->room_id == "42");
    CHECK(snap->unique_chat == 1);

    now += 60'001;
    store.ingest(privmsg(":fresh!fresh@fresh.tmi.twitch.tv PRIVMSG #dallas :new"));
    snap = store.snapshot("dallas");
    CHECK(snap->messages == 1);
    CHECK(snap->timeouts == 0);
    CHECK(snap->chat_clears == 0);

    HttpRequest request;
    CHECK(parseHttpRequest("GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n", request, error));
    CHECK(request.path == "/health");
    CHECK(parseHttpRequest("GET /v1/status?api_key=secret HTTP/1.1\r\nHost: localhost\r\n\r\n", request, error));
    CHECK(request.path == "/v1/status");
    CHECK(request.headers.count("api_key") == 0);

    HttpRequest authed;
    CHECK(parseHttpRequest("GET /v1/status HTTP/1.1\r\nAuthorization: Bearer test-api-key-value\r\n\r\n", authed, error));
    CHECK(requestAuthorized(authed, "test-api-key-value"));
    CHECK(!requestAuthorized(authed, "wrong-api-key-value"));
    CHECK(serializeHttpResponse(HttpResponse{200, "{\"status\":\"ok\"}", "application/json"}).find("test-api-key") ==
          std::string::npos);

    ::setenv("IRCP_API_KEY", "test-api-key-value", 1);
    ::setenv("IRCP_BIND", "127.0.0.1", 1);
    ::setenv("IRCP_PORT", "8080", 1);
    ::setenv("TWITCH_NICK", "somebot", 1);
    ::setenv("TWITCH_OAUTH_TOKEN", "super-secret-token", 1);
    ::setenv("IRCP_CHANNELS", "dallas", 1);
    ::setenv("IRCP_WINDOW_SECONDS", "900", 1);
    ::setenv("IRCP_MAX_CHANNELS", "20", 1);
    ::unsetenv("IRCP_LOG_LEVEL");
    Config config = Config::fromEnvironment();
    AnalyticsStore live(config.windowSeconds());
    TwitchClient client(config, live);
    const std::string status = client.statusJson().dump();
    CHECK(status.find("super-secret-token") == std::string::npos);
    CHECK(status.find("oauth") == std::string::npos);
    CHECK(status.find("\"read_only\":true") != std::string::npos);
    CHECK(status.find("\"message_text_retained\":false") != std::string::npos);
    CHECK(status.find("\"persists_chat\":false") != std::string::npos);
    CHECK(status.find("irc.chat.twitch.tv") != std::string::npos);
    CHECK(client.watch("not a channel") == WatchStatus::Invalid);
    CHECK(client.watch("dallas") == WatchStatus::Duplicate);

    const auto demo = demoSnapshot(
        {{"connected", true}, {"authenticated", true}, {"nick", "somebot"}, {"last_error", "oauth:secret"}},
        {{"channels", nlohmann::json::array({{{"channel", "dallas"},
                                              {"join_state", "joined"},
                                              {"messages", 3},
                                              {"messages_per_minute", 1.5},
                                              {"unique_chatters", 2},
                                              {"top_chatters", {{{"login", "viewer123"}, {"messages", 2}}}},
                                              {"room", {{"room_id", "42"}, {"emote_only", false}}}}})}});
    const std::string demo_dump = demo.dump();
    CHECK(demo["channels"][0]["channel"] == "dallas");
    CHECK(!demo["channels"][0].contains("top_chatters"));
    CHECK(!demo["channels"][0]["room"].contains("room_id"));
    CHECK(demo_dump.find("viewer123") == std::string::npos);
    CHECK(demo_dump.find("somebot") == std::string::npos);
    CHECK(demo_dump.find("oauth:secret") == std::string::npos);
    CHECK(demoPageHtml().find("/demo/snapshot") != std::string::npos);
    CHECK(demoPageHtml().find("oauth:") == std::string::npos);

    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
