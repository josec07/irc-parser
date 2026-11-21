#include "ircp/demo.h"

namespace ircp {
namespace {

const char* kDemoPage = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>irc-parser demo</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; min-height: 100vh;
    background: #10140f; color: #e7efe4;
    font: 15px/1.4 ui-sans-serif, system-ui, sans-serif;
    display: grid; place-items: center; padding: 24px;
  }
  main { width: min(760px, 100%); }
  header, footer { display: flex; justify-content: space-between; gap: 12px; align-items: center; }
  h1 { font-size: 15px; font-weight: 650; letter-spacing: .04em; margin: 0; }
  .live { display: flex; align-items: center; gap: 8px; color: #c6f54e; font-size: 12px; letter-spacing: .12em; }
  .dot { width: 8px; height: 8px; border-radius: 50%; background: #c6f54e; box-shadow: 0 0 0 0 rgba(198,245,78,.7); animation: pulse 1.6s infinite; }
  .offline { color: #e7c36a; }
  .offline .dot { background: #e7c36a; animation: none; box-shadow: none; }
  @keyframes pulse { 70% { box-shadow: 0 0 0 8px rgba(198,245,78,0); } 100% { box-shadow: 0 0 0 0 rgba(198,245,78,0); } }
  .channel { margin: 22px 0 6px; font-size: 28px; font-weight: 650; }
  .meta { color: #8b9a86; font-size: 13px; min-height: 1.2em; }
  .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 18px; }
  .card { background: #1a2118; border: 1px solid #2a3326; border-radius: 12px; padding: 14px 14px 12px; }
  .card span { display: block; color: #8b9a86; font-size: 12px; letter-spacing: .04em; }
  .card strong { display: block; margin-top: 6px; font: 28px/1 ui-monospace, monospace; font-weight: 560; }
  .card.bump strong { color: #c6f54e; }
  svg { width: 100%; height: 72px; margin-top: 16px; display: block; }
  .pills { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 14px; min-height: 26px; }
  .pill { border: 1px solid #2a3326; border-radius: 999px; padding: 3px 8px; color: #8b9a86; font-size: 12px; }
  footer { margin-top: 22px; color: #6d7b69; font-size: 12px; }
  @media (max-width: 640px) { .grid { grid-template-columns: 1fr 1fr; } .channel { font-size: 22px; } }
</style>
</head>
<body>
<main>
  <header>
    <h1>irc-parser</h1>
    <div class="live" id="state"><i class="dot"></i><span>LIVE</span></div>
  </header>
  <div class="channel" id="channel">waiting</div>
  <div class="meta" id="meta">connecting</div>
  <section class="grid" id="grid"></section>
  <svg id="spark" viewBox="0 0 240 72" preserveAspectRatio="none" aria-hidden="true"></svg>
  <div class="pills" id="pills"></div>
  <footer>
    <span>Unofficial. Not affiliated with Twitch. Counts only. No chat text, no chatter names.</span>
    <span id="window"></span>
  </footer>
</main>
<script>
const fields = [
  ["messages_per_minute", "per minute", v => Number(v).toFixed(1)],
  ["messages", "messages", v => v],
  ["unique_chatters", "chatters", v => v],
  ["commands", "commands", v => v],
  ["bits", "bits", v => v],
  ["subscriptions", "subs", v => v],
  ["raids", "raids", v => v],
  ["gifted_subscriptions", "gifts", v => v],
  ["resubscriptions", "resubs", v => v]
];
const history = [];
const last = {};
function card(key, label, text, changed) {
  return '<div class="card' + (changed ? ' bump' : '') + '"><span>' + label + '</span><strong>' + text + '</strong></div>';
}
function spark() {
  if (history.length < 2) { document.getElementById('spark').innerHTML = ''; return; }
  const max = Math.max(1, ...history);
  const step = 240 / (history.length - 1);
  const pts = history.map((v, i) => (i * step).toFixed(1) + ',' + (64 - (v / max) * 52).toFixed(1)).join(' ');
  document.getElementById('spark').innerHTML = '<polyline fill="none" stroke="#c6f54e" stroke-width="2" points="' + pts + '"/>';
}
function render(data) {
  const state = document.getElementById('state');
  const up = data.connected && data.authenticated && !data.auth_rejected;
  state.className = up ? 'live' : 'live offline';
  state.querySelector('span').textContent = data.auth_rejected ? 'TOKEN' : (up ? 'LIVE' : 'OFFLINE');
  const ch = (data.channels && data.channels[0]) || null;
  document.getElementById('channel').textContent = ch ? '#' + ch.channel : 'no channel';
  document.getElementById('meta').textContent = ch
    ? ch.join_state + ' · updated ' + new Date((ch.generated_at || 0) * 1000).toLocaleTimeString()
    : 'set IRCP_CHANNELS and restart';
  document.getElementById('window').textContent = ch ? ch.window_seconds + 's window' : '';
  document.getElementById('grid').innerHTML = fields.map(([key, label, fmt]) => {
    const text = ch ? String(fmt(ch[key] || 0)) : '0';
    const changed = last[key] !== undefined && last[key] !== text;
    last[key] = text;
    return card(key, label, text, changed);
  }).join('');
  const room = ch && ch.room || {};
  const pills = [];
  if (room.emote_only) pills.push('emote-only');
  if (room.subs_only) pills.push('subs-only');
  if (room.unique_chat) pills.push('unique-chat');
  if (room.slow_seconds) pills.push('slow ' + room.slow_seconds + 's');
  if (room.followers_only > -1) pills.push('followers ' + room.followers_only);
  document.getElementById('pills').innerHTML = pills.map(p => '<span class="pill">' + p + '</span>').join('');
  history.push(ch ? Number(ch.messages_per_minute) || 0 : 0);
  if (history.length > 24) history.shift();
  spark();
}
async function tick() {
  try {
    const res = await fetch('/demo/snapshot', {cache: 'no-store'});
    render(await res.json());
  } catch (err) {
    document.getElementById('meta').textContent = 'snapshot unavailable';
  }
}
tick();
setInterval(tick, 2000);
</script>
</body>
</html>
)HTML";

}  // namespace

std::string demoPageHtml() { return kDemoPage; }

nlohmann::json demoSnapshot(const nlohmann::json& status, const nlohmann::json& channel_list) {
    nlohmann::json out = {{"unofficial", true},
                          {"affiliated", false},
                          {"read_only", true},
                          {"message_text_retained", false},
                          {"chatter_names_included", false},
                          {"connected", status.value("connected", false)},
                          {"authenticated", status.value("authenticated", false)},
                          {"auth_rejected", status.value("auth_rejected", false)},
                          {"channels", nlohmann::json::array()}};
    if (!channel_list.contains("channels") || !channel_list["channels"].is_array()) {
        return out;
    }
    for (const auto& channel : channel_list["channels"]) {
        if (!channel.is_object()) {
            continue;
        }
        nlohmann::json room = nlohmann::json::object();
        if (channel.contains("room") && channel["room"].is_object()) {
            const auto& source = channel["room"];
            room = {{"emote_only", source.value("emote_only", false)},
                    {"followers_only", source.value("followers_only", -1)},
                    {"slow_seconds", source.value("slow_seconds", 0)},
                    {"subs_only", source.value("subs_only", false)},
                    {"unique_chat", source.value("unique_chat", false)}};
        }
        out["channels"].push_back({{"channel", channel.value("channel", "")},
                                   {"join_state", channel.value("join_state", "pending")},
                                   {"messages", channel.value("messages", 0)},
                                   {"messages_per_minute", channel.value("messages_per_minute", 0.0)},
                                   {"unique_chatters", channel.value("unique_chatters", 0)},
                                   {"commands", channel.value("commands", 0)},
                                   {"bits", channel.value("bits", 0)},
                                   {"subscriptions", channel.value("subscriptions", 0)},
                                   {"resubscriptions", channel.value("resubscriptions", 0)},
                                   {"gifted_subscriptions", channel.value("gifted_subscriptions", 0)},
                                   {"raids", channel.value("raids", 0)},
                                   {"window_seconds", channel.value("window_seconds", 0)},
                                   {"generated_at", channel.value("generated_at", 0)},
                                   {"room", std::move(room)}});
    }
    return out;
}

}  // namespace ircp
