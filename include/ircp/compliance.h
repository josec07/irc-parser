#pragma once

namespace ircp {

// Tripwires for the controls described in COMPLIANCE.md. The client also
// enforces these at runtime (TLS-only host, outbound command allowlist, no
// message-text field on stored events). Flipping a flag here should fail a
// static_assert or a unit test before it can ship.
struct Compliance {
    static constexpr bool kReadOnly = true;
    static constexpr bool kPersistsChat = false;
    static constexpr bool kRetainsMessageText = false;
    static constexpr bool kRequestsMembership = false;
    static constexpr bool kUsesTls = true;
    static constexpr bool kHostConfigurable = false;

    // Documented Twitch limits for a normal account, as of the chat docs:
    // 100 concurrent joins, 20 joins / 10s, 20 authentications / 10s.
    // https://dev.twitch.tv/docs/chat
    static constexpr int kHardMaxChannels = 100;
    static constexpr int kHardMaxWindowSeconds = 24 * 60 * 60;
    static constexpr int kJoinSpacingMs = 250;
    static constexpr int kMaxJoinsPer10s = 18;

    static constexpr const char* kIrcHost = "irc.chat.twitch.tv";
    static constexpr int kIrcPort = 6697;
    static constexpr const char* kValidateHost = "id.twitch.tv";
};

}  // namespace ircp
