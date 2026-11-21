# Twitch compliance

This is not legal advice. You still have to follow the [Twitch Developer Services Agreement](https://www.twitch.tv/p/legal/developer-agreement/), the [Terms of Service](https://www.twitch.tv/p/legal/terms-of-service/), and the [chat docs](https://dev.twitch.tv/docs/chat). Twitch can change those rules. This program is not affiliated with Twitch.

It is a self-hosted, read-only analytics process for channels the operator explicitly configures. It is not a public chat archive, a scraper, or a bot that speaks in chat.

## What it will not do

| Control | Why |
| --- | --- |
| TLS only, `irc.chat.twitch.tv:6697` | The OAuth token is never sent in cleartext. The host is not configurable, so the token cannot be pointed at another server. |
| Documented user-token auth | [IRC auth](https://dev.twitch.tv/docs/chat/irc) requires a user access token. Anonymous `justinfan` / `SCHMOOPIIE` is not implemented. |
| `chat:read` only | Startup calls `https://id.twitch.tv/oauth2/validate` and refuses a token without `chat:read`. `chat:edit` is unused and warned on. |
| No `PRIVMSG`, whisper, or moderation commands | Outbound IRC is an allowlist: `CAP`, `PASS`, `NICK`, `JOIN`, `PART`, `PONG`, `QUIT`. Send-message rate limits never apply because nothing is sent. |
| No `twitch.tv/membership` | `JOIN` / `PART` presence is not needed for aggregates, and it is a list of who is in chat. Names lists (`353`) are ignored. |
| No message text, no disk | Events store counts, command names (`!song`), and emote ids. There is no message field. Nothing is written to disk, so Docker logs are not a chat log either. |
| Moderation is honored | `CLEARCHAT` drops that user's messages from the window, or the whole window on a full clear. `CLEARMSG` drops the matching message id. |
| Short window | Default 15 minutes. Hard cap 24 hours. The Developer Agreement says not to retain chat logs to build a public database. |
| Join limits | Hard cap 100 channels, the [documented concurrent join limit](https://dev.twitch.tv/docs/chat#concurrent-join-limits). Joins are paced at 250ms and at most 18 per 10 seconds, under the 20/10s join limit. |
| Auth failures do not retry | A rejected token is not hammered. Network reconnects back off from 2s to 60s, under the 20 authentications / 10s limit. `RECONNECT` is honored. |
| No channel discovery | Channels come from `IRCP_CHANNELS` or `POST /v1/channels`. Other hosts and non-Twitch URLs are rejected. |
| API key, no query-string secrets | `/health` returns only `{"status":"ok"}`. Everything else requires `Authorization: Bearer` or `X-API-Key`. Compose publishes `127.0.0.1:8080` only. |

## What you still have to do

- Register your own Twitch application and use the [official auth flow](https://dev.twitch.tv/docs/authentication/). Request `chat:read` only.
- `TWITCH_NICK` must be the login of the account that granted the token.
- Watch your own channel, or a channel you have a real reason to analyze. Do not point this at a list of channels you have no relationship with.
- Do not put the API on the public internet without the key, and do not republish the JSON as a chat product.
- If a channel suspends you or bans the account, the process stops rejoining that channel. Do not work around that.
- Refresh the token before it expires. `/v1/status` reports `token_expires_in`.

The running process reports the same limits on `GET /v1/status`: `read_only`, `tls`, `membership_capability`, `persists_chat`, and `message_text_retained`.
