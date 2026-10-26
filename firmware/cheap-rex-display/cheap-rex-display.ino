// cheap-rex-display firmware.
//
// Joins your Wi-Fi as crd.local and lists the agent status it is sent over
//  HTTP, most urgent first. The RGB LED blinks once for each agent that waits
//  for you, in the color of what it waits for, and otherwise shows the most
//  urgent state (see LedStyle). While nothing waits, a calm screen counts the
//  agents by state instead; a tap shows the list for LIST_MS. Display only:
//  nothing here can answer or approve anything in an agent session.
//
// HTTP API, form or query parameters, plain-text replies unless noted:
//   POST /status  source, key, session, app, state, kind, msg
//                 source names the Rex server, key the pane and record.
//                 state is idle, working, done, blocked, error or clear.
//                 clear removes key and every key below it (key#..., key/...).
//   POST /heartbeat  source. Replies "known", or "unknown" when this board has
//                 nothing from that source (after a restart), so the watcher
//                 sends everything again.
//   POST /reset   source. Drops everything from that source.
//   GET  /        plain-text dump of what the display holds.
//   GET  /screen.bmp  what the screen shows, read back from the display.
//   GET  /api/settings  the settings as JSON:
//                 {"rows_compact":false,"header_counts":true,"calm_screen":true}
//   POST /api/settings  rows_compact, header_counts, calm_screen: true|false;
//                 replies with the settings.
//   GET  /settings  the settings page: loads the settings UI from the website.
// A source that sends nothing for STALE_MS is shown grey and ignored by the
//  LED, so a sleeping laptop leaves no "working" behind.
//
// Taps go back to the watchers on port 81. A watcher keeps one request open,
//  GET /taps?source=NAME, and reads a line "tap KEY" for each tap on one of its
//  rows, and "ping" every PING_MS. A new request for a source replaces the old.
//
// Wi-Fi comes from the first of these on boot:
//   1. the network saved in flash, by the setup hotspot or by Improv;
//   2. WIFI_SSID and WIFI_PASSWORD from secrets.h, if the file is there;
//   3. none: the board opens its setup hotspot.
// The setup hotspot is the board's own Wi-Fi, crd-setup-XXXX, with a new WPA2
//  password each time it opens. The screen shows both. Its page at 192.168.4.1
//  is a captive portal, so phones and computers open it by themselves. It lists the networks
//  in range; Save joins the one picked, saves it and restarts. The hotspot also
//  opens when the network in use can't be joined for SETUP_AFTER_MS, and keeps
//  trying that network while no device is on the hotspot.
// Improv Wi-Fi over serial (improv_serial.cpp) works at all times, so ESP Web
//  Tools can send Wi-Fi over USB right after flashing.
// The boot splash runs while Wi-Fi connects. With a network to join it waits for
//  it as before, then shows the status screen, which hands over to the hotspot
//  once SETUP_AFTER_MS have passed without Wi-Fi. With none, the splash says so
//  on its bottom line, plays to the end (or a tap) and the hotspot opens.
// Holding a finger on the splash for FORGET_HOLD_MS offers to forget the saved
//  network, with a tap to confirm. Nothing over HTTP can forget it.
// Passwords never go to Serial or the screen, except the hotspot's own.
//
// Settings in flash: Preferences (NVS) namespace "crd", one namespace for
//  everything the board keeps. A normal upload keeps it,
//  a full flash erase wipes it.
//  Keys:
//   wifi_ssid    string  saved network
//   wifi_pass    string  its password
//   rows_compact bool    compact rows, toggled by the button at the right of the header
//   header_counts bool   the counts in the header over the list, on unless set
//   calm_screen  bool    the calm screen while nothing waits, on unless set
//  Reserved for the settings screen, not read yet:
//   led_count    bool    LedStyle.countBlocked
//   led_double   int     LedStyle.doubleAfterMin
//   led_breathe  bool    LedStyle.workingBreathes
//   led_glow     bool    LedStyle.doneGlows
//   header_rex   uint8   0 REX_PIXEL, 1 REX_SOFT, 2 REX_BOLD
//  Forgetting Wi-Fi removes the two wifi_ keys and nothing else.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <algorithm>
#include <TFT_eSPI.h>             // settings from config/TFT_eSPI/User_Setup.h, see the README
#include <XPT2046_Touchscreen.h>
#if __has_include("secrets.h")
#include "secrets.h"              // optional: WIFI_SSID and WIFI_PASSWORD, from secrets.h.example
#endif
#include "improv_serial.h"
#include "rex_art.h"

#define HOSTNAME "crd"
#define FIRMWARE_NAME "cheap-rex-display"
#define FIRMWARE_VERSION "0.2.0"  // for Improv: ESP Web Tools offers an update when its manifest differs

#define SETTINGS_NAMESPACE "crd"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASSWORD "wifi_pass"
#define KEY_ROWS_COMPACT "rows_compact"
#define KEY_HEADER_COUNTS "header_counts"
#define KEY_CALM_SCREEN "calm_screen"

// The settings UI lives on the website. SETTINGS_UI_SRI is the sha384 of that
//  exact file, so a board only runs the UI release it was built for.
#define SETTINGS_SITE "https://cheap-rex-display.pages.dev"
#ifndef SETTINGS_UI
#define SETTINGS_UI SETTINGS_SITE "/prototypes/settings-ui.js"
#endif
#ifndef SETTINGS_UI_SRI
#define SETTINGS_UI_SRI "sha384-hivIFvtD2UAZlb4TCOeiy7s1KZXShn3bUktgBwOeLurTFK/sTlZBzp1+q/V6xmZD"
#endif

// Joining Wi-Fi. While offline the board starts over every REJOIN_MS. The
//  hotspot opens once the network in use couldn't be joined for SETUP_AFTER_MS:
//  longer than most routers take to start (1 to 2 minutes), so a power cut that
//  restarts both doesn't send the board to setup, yet short enough for someone
//  who moved it to another network. Setup keeps trying the old network, so
//  opening a little early costs nothing.
#define REJOIN_MS 30000
#define SETUP_AFTER_MS 180000
// A network picked in setup or sent by Improv gets this long to join, well
//  within the 45 s ESP Web Tools waits.
#define JOIN_MS 20000
#define HOTSPOT_PASSWORD_LEN 8
#define MAX_NETWORKS 16
// Forgetting Wi-Fi: hold the splash this long; a hint shows after FORGET_HINT_MS,
//  and the question waits FORGET_ASK_MS for its tap.
#define FORGET_HOLD_MS 5000
#define FORGET_HINT_MS 1000
#define FORGET_ASK_MS 10000

// Rex at the left of the header: REX_PIXEL, REX_SOFT or REX_BOLD
#define HEADER_REX REX_PIXEL

// Boot splash: Rex traces himself while Wi-Fi connects, then the words are
//  written in. It waits for Wi-Fi up to SPLASH_MAX_MS, and a tap skips it.
//  With no network to join it doesn't wait.
#define SPLASH_REX_X 22
#define SPLASH_REX_Y 52
#define SPLASH_REX_H 136
#define SPLASH_TRACE_MS 3200
#define SPLASH_WORD_MS 360
#define SPLASH_HOLD_MS 1500
#define SPLASH_MAX_MS 10000
// The calm screen, shown when nothing waits: the splash Rex stands above two
//  lines, the counts and how long it has been quiet, and wobbles:
//  WOBBLE_DRAWINGS drawings of him, each point moved by up to WOBBLE_PX, take
//  turns every WOBBLE_MS, like hand-drawn animation. His 1-bit canvas leaves
//  WOBBLE_PX + 3 around him.
#define WOBBLE_PX 1.0f
#define WOBBLE_DRAWINGS 3
#define WOBBLE_MS 250
#define WOBBLE_W 148
#define WOBBLE_H 144
#define WOBBLE_X ((SCREEN_W - WOBBLE_W) / 2)
#define WOBBLE_Y 46
#define CALM_COUNTS_Y 207
#define CALM_QUIET_Y 230
#define TAP_PORT 81
#define PING_MS 15000

// RGB LED, active low.
#define LED_RED 4
#define LED_GREEN 16
#define LED_BLUE 17

// Touch controller, on its own SPI bus.
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
#define TOUCH_CS 33
// Raw touch range, measured edge to edge in rotation 1.
#define TOUCH_X_MIN 275
#define TOUCH_X_MAX 3630
#define TOUCH_Y_MIN 380
#define TOUCH_Y_MAX 3730
// The first readings of a press drift by several pixels while the finger lands.
#define TOUCH_SETTLE_MS 30
// A press has ended once no touch was seen for this long.
#define TOUCH_RELEASE_MS 60
#define TAP_FLASH_MS 300

#define SCREEN_W 320
#define SCREEN_H 240
#define HEADER_H 24
#define ROW_H_FINGER 43   // 5 rows; resistive touch wants targets of 40 px or more
#define ROW_H_COMPACT 24  // 9 rows, for the stylus
#define MAX_SLOTS ((SCREEN_H - HEADER_H) / ROW_H_COMPACT)
#define PAGE_MS 30000     // a later page goes back to the first after this long untouched
// The list stays this long after the last tap on it, or after the last agent
//  stopped waiting, before the calm screen comes back: enough to read and tap
//  a row, and to see an agent that waits again a moment later in the same place.
#define LIST_MS 60000
#define MAX_ENTRIES 32
#define MAX_SOURCES 4
#define STALE_MS 60000
#define REDRAW_MS 200

#define COLOR_GREY 0x7BEF
#define COLOR_DIVIDER 0x2104
#define COLOR_BUTTON 0x31A6
#define PAGE_BUTTON_W 110
#define TOGGLE_W 40  // the row-size button at the right of the header, and its tap area

// In order of urgency: rows and the LED go by the lowest value.
enum State { BLOCKED, ERROR_, DONE, WORKING, IDLE, STATE_COUNT };
const char *stateNames[] = {"blocked", "error", "done", "working", "idle"};
const uint16_t stateColors[] = {TFT_RED, TFT_MAGENTA, TFT_GREEN, TFT_BLUE, COLOR_GREY};

struct Entry {
  bool used;
  char source[16];
  char key[80];
  char session[24];
  char app[16];
  char kind[12];
  char msg[64];
  State state;
  unsigned long since;  // when it last changed state
};

// How the LED speaks. The defaults were picked on the board; a settings
//  screen can change them while it runs.
struct LedStyle {
  bool countBlocked;     // one signal per blocked agent, up to 4, instead of one for all
  int doubleAfterMin;    // permission blinks twice ("._.") once it waited this long: 0 at once, -1 never
  bool workingBreathes;  // slow blue breathing while agents work, else steady dim blue
  bool doneGlows;        // a green glow when an agent finishes, then the LED moves on; else steady green
};

// One LED pulse: lit for `on` ms with a smooth rise and fall, then dark for `after` ms
struct Pulse {
  uint8_t r, g, b;
  uint16_t on, after;
};

struct Source {
  bool used;
  char name[16];
  unsigned long lastSeen;
};

// What each drawn row shows, so a tap still names the right pane after the
//  entries change underneath it.
struct Row {
  char source[16];
  char key[80];
  int entry;    // index into entries, for redrawing the row alone
  char age[8];  // the time shown at the right, "" under a minute
};

// Kept apart from sources: an open tap request must not make the board
//  answer "known" to a heartbeat when it holds nothing from that source.
struct TapClient {
  char source[16];
  NetworkClient client;
};

// Where the network in use came from, see the top of this file.
enum WifiFrom { WIFI_NONE, WIFI_SAVED, WIFI_SECRETS };

// A network the setup page offers
struct SeenNetwork {
  String ssid;
  int rssi;
  bool open;
};

Entry entries[MAX_ENTRIES];
Source sources[MAX_SOURCES];
TapClient tapClients[MAX_SOURCES];
Row drawnRows[MAX_ENTRIES];
int drawnCount = 0;
TFT_eSPI tft = TFT_eSPI();
// The header and each row slot are drawn off-screen in here and sent in one go,
//  so the screen never shows a slot cleared and half drawn. One finger-sized
//  row at 16 bits is 27.5 KB.
TFT_eSprite canvas = TFT_eSprite(&tft);
// A hash of what the header (0) and each row slot (1 on) last sent, so slots
//  that come out the same are not sent again. 0 means unknown.
uint32_t shownSlots[MAX_SLOTS + 1];
// The wobbling Rex of the calm screen, 2.6 KB
TFT_eSprite wobbleCanvas = TFT_eSprite(&tft);
bool rexShown = false;  // the calm screen is up, and its Rex wobbles
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
WebServer server(80);
NetworkServer tapServer(TAP_PORT);
LedStyle ledStyle = {true, 0, true, true};
unsigned long doneAt = 0;  // when an agent last finished, for the green glow
bool dirty = true;
unsigned long lastDraw = 0;
unsigned long holdUntil = 0;  // a tapped row's outline stays up until then
bool compactRows = false;
bool headerCounts = true;
bool calmEnabled = true;  // off: the list stays, and the calm screen shows only with no rows at all
int page = 0;                 // which page of rows is shown, when they don't all fit
unsigned long pageAt = 0;     // when the page last changed
int pageCount = 1;
int moreSlot = -1;            // the slot that holds the page buttons, or -1 when all rows fit
unsigned long listUntil = 0;  // the list stays instead of the calm screen until then; 0 when not
unsigned long quietSince = 0; // when the last agent stopped waiting, 0 while none has waited
char shownQuiet[32];          // the calm screen's quiet line as drawn

String wifiSsid, wifiPassword;  // the network in use
WifiFrom wifiFrom = WIFI_NONE;
bool serving = false;           // the status and tap servers are up
bool inSetup = false;
// A Wi-Fi join that failed drew over the screen, so whoever owns it draws it again.
bool screenTaken = false;

// Setup mode
const IPAddress HOTSPOT_IP(192, 168, 4, 1);
WebServer portal(80);           // takes port 80 once the status server stops
DNSServer dns;
char hotspotName[20];
char hotspotPassword[HOTSPOT_PASSWORD_LEN + 1];
String setupNote;               // the bottom line of the setup screen
String pendingSsid, pendingPassword;  // picked on the page, joined once the reply is out
SeenNetwork networks[MAX_NETWORKS];
int networkCount = 0;

// The fonts only cover ASCII. Each UTF-8 character outside it becomes '?'.
void copyAscii(char *dst, size_t size, const String &src) {
  size_t n = 0;
  for (size_t i = 0; i < src.length() && n + 1 < size; i++) {
    uint8_t c = src[i];
    // Skip UTF-8 continuation bytes
    if (c >= 0x80 && c < 0xC0) {
      continue;
    }

    dst[n++] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  dst[n] = 0;
}

int parseState(const String &s) {
  for (int i = 0; i < STATE_COUNT; i++) {
    if (s == stateNames[i]) {
      return i;
    }
  }

  return -1;
}

Source *findSource(const char *name, bool create) {
  Source *freeSlot = nullptr;
  for (auto &s : sources) {
    if (s.used && strcmp(s.name, name) == 0) {
      return &s;
    }

    if (!s.used && !freeSlot) {
      freeSlot = &s;
    }
  }

  if (!create || !freeSlot) {
    return nullptr;
  }

  freeSlot->used = true;
  strlcpy(freeSlot->name, name, sizeof(freeSlot->name));
  return freeSlot;
}

bool isStale(const char *name) {
  Source *s = findSource(name, false);
  return !s || millis() - s->lastSeen > STALE_MS;
}

// Marks a source as alive. Returns false if it was unknown before.
bool touchSource(const String &name) {
  char buf[16];
  copyAscii(buf, sizeof(buf), name);

  bool known = findSource(buf, false) != nullptr;
  Source *s = findSource(buf, true);

  if (s) {
    s->lastSeen = millis();
  }
  dirty = true;
  return known;
}

// True if key is parent itself or sits below it.
bool keyCovers(const char *parent, const char *key) {
  size_t n = strlen(parent);
  return strncmp(parent, key, n) == 0 && (key[n] == 0 || key[n] == '#' || key[n] == '/');
}

void handleStatus() {
  String source = server.arg("source"), key = server.arg("key"), stateArg = server.arg("state");
  if (source.isEmpty() || key.isEmpty() || stateArg.isEmpty()) {
    server.send(400, "text/plain", "source, key and state are required\n");
    return;
  }

  touchSource(source);
  char src[16], k[80];
  copyAscii(src, sizeof(src), source);
  copyAscii(k, sizeof(k), key);

  if (stateArg == "clear") {
    for (auto &e : entries) {
      if (e.used && strcmp(e.source, src) == 0 && keyCovers(k, e.key)) {
        e.used = false;
      }
    }

    server.send(200, "text/plain", "ok\n");
    return;
  }
  int state = parseState(stateArg);
  if (state < 0) {
    server.send(400, "text/plain", "unknown state\n");
    return;
  }

  Entry *slot = nullptr;
  for (auto &e : entries) {
    if (e.used && strcmp(e.source, src) == 0 && strcmp(e.key, k) == 0) {
      slot = &e;
      break;
    }
  }

  for (auto &e : entries) {
    if (!slot && !e.used) {
      slot = &e;
    }
  }

  if (!slot) {
    server.send(507, "text/plain", "full\n");
    return;
  }
  if (!slot->used || slot->state != state) {
    slot->since = millis();
    if (state == DONE) {
      doneAt = millis();
    }
  }

  slot->used = true;
  strlcpy(slot->source, src, sizeof(slot->source));
  strlcpy(slot->key, k, sizeof(slot->key));
  copyAscii(slot->session, sizeof(slot->session), server.arg("session"));
  copyAscii(slot->app, sizeof(slot->app), server.arg("app"));
  copyAscii(slot->kind, sizeof(slot->kind), server.arg("kind"));
  copyAscii(slot->msg, sizeof(slot->msg), server.arg("msg"));
  slot->state = (State)state;
  server.send(200, "text/plain", "ok\n");
}

void handleHeartbeat() {
  if (server.arg("source").isEmpty()) {
    server.send(400, "text/plain", "source is required\n");
    return;
  }

  server.send(200, "text/plain", touchSource(server.arg("source")) ? "known\n" : "unknown\n");
}

void handleReset() {
  String source = server.arg("source");
  if (source.isEmpty()) {
    server.send(400, "text/plain", "source is required\n");
    return;
  }

  touchSource(source);
  char src[16];
  copyAscii(src, sizeof(src), source);

  for (auto &e : entries) {
    if (e.used && strcmp(e.source, src) == 0) {
      e.used = false;
    }
  }

  server.send(200, "text/plain", "ok\n");
}

TapClient *findTapClient(const char *source) {
  for (auto &t : tapClients) {
    if (t.client.connected() && strcmp(t.source, source) == 0) {
      return &t;
    }
  }

  return nullptr;
}

// Takes a new tap request on port 81 and keeps it open for its source.
void acceptTapClient() {
  if (!tapServer.hasClient()) {
    return;
  }

  NetworkClient client = tapServer.accept();
  client.setTimeout(500);
  String request = client.readStringUntil('\n');  // GET /taps?source=NAME HTTP/1.1
  while (client.available()) {
    client.read();  // the rest of the headers
  }

  int start = request.indexOf("source=");
  int end = request.indexOf(' ', start);
  String name = start < 0 ? "" : request.substring(start + 7, end < 0 ? request.length() : end);
  if (!request.startsWith("GET /taps") || name.isEmpty()) {
    client.print("HTTP/1.0 400 Bad Request\r\n\r\nGET /taps?source=NAME\n");
    client.stop();
    return;
  }

  char src[16];
  copyAscii(src, sizeof(src), name);
  TapClient *slot = findTapClient(src);

  for (auto &t : tapClients) {
    if (!slot && !t.client.connected()) {
      slot = &t;
    }
  }

  if (!slot) {
    client.print("HTTP/1.0 507 Insufficient Storage\r\n\r\nfull\n");
    client.stop();
    return;
  }

  slot->client.stop();
  strlcpy(slot->source, src, sizeof(slot->source));
  client.print("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
  slot->client = client;
  Serial.printf("Taps: %s connected\n", src);
}

// A ping now and then shows whether each watcher is still there: writing to
//  a closed connection fails.
void pingTapClients() {
  static unsigned long lastPing = 0;
  if (millis() - lastPing < PING_MS) {
    return;
  }

  lastPing = millis();
  for (auto &t : tapClients) {
    if (t.client.connected() && t.client.print("ping\n") == 0) {
      t.client.stop();
    }
  }
}

// Sends a tap to the watcher of the row's source.
// Returns false if none is connected.
bool sendTap(const Row &row) {
  TapClient *t = findTapClient(row.source);
  if (!t) {
    return false;
  }

  if (t->client.printf("tap %s\n", row.key) == 0) {
    t->client.stop();
    return false;
  }

  return true;
}

void handleDump() {
  String out;
  for (auto &s : sources) {
    if (!s.used) {
      continue;
    }

    out += String("source ") + s.name + " seen " + (millis() - s.lastSeen) / 1000 + " s ago\n";
  }

  for (auto &e : entries) {
    if (!e.used) {
      continue;
    }

    out += String(e.source) + " | " + e.session + " | " + e.app + " | " + stateNames[e.state];
    if (e.kind[0]) {
      out += String(" (") + e.kind + ")";
    }

    out += String(" | ") + e.msg + " | " + e.key + " | for " + (millis() - e.since) / 1000 + " s\n";
  }
  server.send(200, "text/plain", out.isEmpty() ? String("empty\n") : out);
}

void putLE(uint8_t *p, uint32_t v, int bytes) {
  for (int i = 0; i < bytes; i++) {
    p[i] = v >> (8 * i);
  }
}

// Reads the screen back from the display, one line at a time, so no
//  framebuffer is needed. BMP stores the bottom line first, in BGR order.
void handleScreen() {
  const uint32_t lineBytes = SCREEN_W * 3;  // 960, already a multiple of 4
  const uint32_t pixelBytes = lineBytes * SCREEN_H;
  uint8_t header[54] = {'B', 'M'};
  putLE(header + 2, 54 + pixelBytes, 4);
  putLE(header + 10, 54, 4);                // pixel data offset
  putLE(header + 14, 40, 4);                // BITMAPINFOHEADER size
  putLE(header + 18, SCREEN_W, 4);
  putLE(header + 22, SCREEN_H, 4);
  putLE(header + 26, 1, 2);                 // planes
  putLE(header + 28, 24, 2);                // bits per pixel
  putLE(header + 34, pixelBytes, 4);

  server.setContentLength(sizeof(header) + pixelBytes);
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)header, sizeof(header));

  static uint8_t line[SCREEN_W * 3];
  for (int y = SCREEN_H - 1; y >= 0; y--) {
    tft.readRectRGB(0, y, SCREEN_W, 1, line);
    for (int x = 0; x < SCREEN_W; x++) {
      uint8_t r = line[x * 3];
      line[x * 3] = line[x * 3 + 2];
      line[x * 3 + 2] = r;
    }
    server.sendContent((const char *)line, lineBytes);
  }
}

// The settings, for the settings UI. It runs on the board's own page (below),
//  so it calls these from the same origin and no CORS headers are needed.
void sendSettings() {
  server.send(200, "application/json", String("{\"rows_compact\":") + (compactRows ? "true" : "false")
    + ",\"header_counts\":" + (headerCounts ? "true" : "false")
    + ",\"calm_screen\":" + (calmEnabled ? "true" : "false") + "}\n");
}

// Takes a true|false setting from the request into `value` and flash.
void saveFlag(const char *key, bool &value) {
  if (!server.hasArg(key) || (server.arg(key) == "true") == value) {
    return;
  }

  value = server.arg(key) == "true";
  Preferences settings;
  settings.begin(SETTINGS_NAMESPACE, false);
  settings.putBool(key, value);
  settings.end();
  dirty = true;
}

void handleSettingsSave() {
  for (const char *key : {KEY_ROWS_COMPACT, KEY_HEADER_COUNTS, KEY_CALM_SCREEN}) {
    if (server.hasArg(key) && server.arg(key) != "true" && server.arg(key) != "false") {
      server.send(400, "text/plain", String(key) + " is true or false\n");
      return;
    }
  }

  saveFlag(KEY_HEADER_COUNTS, headerCounts);
  saveFlag(KEY_CALM_SCREEN, calmEnabled);

  if (server.hasArg(KEY_ROWS_COMPACT)) {
    String v = server.arg(KEY_ROWS_COMPACT);
    if ((v == "true") != compactRows) {
      setCompactRows(v == "true");
      page = 0;
      dirty = true;
    }
  }
  sendSettings();
}

// A page from the board's own address that runs the settings UI from the
//  website, so the UI reaches the board without asking the browser for local
//  network access. The integrity hash pins the one UI release
//  this firmware was built for.
void handleSettingsPage() {
  server.send(200, "text/html",
    "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width\">"
    "<title>Cheap Rex Display settings</title><div id=crd>Loading the settings from " SETTINGS_SITE "...</div>"
    "<script type=module src=\"" SETTINGS_UI "\" integrity=\"" SETTINGS_UI_SRI "\" crossorigin=anonymous></script>"
    "<script>addEventListener('error',e=>{if(e.target.tagName==='SCRIPT')crd.textContent="
    "'The settings come from " SETTINGS_SITE ", which this computer could not load. Is it online?'},true)</script>");
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  // Active low: a duty of 255 is off.
  ledcWrite(LED_RED, 255 - r);
  ledcWrite(LED_GREEN, 255 - g);
  ledcWrite(LED_BLUE, 255 - b);
}

// A color at brightness 0..1, squared so fades look even to the eye
void setLevel(uint8_t r, uint8_t g, uint8_t b, float v) {
  v = constrain(v, 0.0f, 1.0f);
  v *= v;
  setLed(r * v, g * v, b * v);
}

// One smooth rise from 0 to 1 and fall back over `ms`
float bump(unsigned long t, unsigned long ms) {
  return t < ms ? sinf(PI * t / ms) : 0;
}

bool listed(const int *order, int count, const Entry &e);

// The pulses for the blocked agents, one pane each: permission first (a short
//  red pulse, or a double blink), then question (long amber), then auth (long
//  orange). Returns how many it wrote, at most 8.
int blockedPattern(Pulse *out) {
  int panes[MAX_ENTRIES], paneCount = 0, n = 0;
  int maxAgents = ledStyle.countBlocked ? 4 : 1;

  for (int rank = 0; rank < 3; rank++) {
    for (int i = 0; i < MAX_ENTRIES && paneCount < maxAgents; i++) {
      const Entry &e = entries[i];
      if (!e.used || e.state != BLOCKED || isStale(e.source) || listed(panes, paneCount, e)) {
        continue;
      }

      int kindRank = strcmp(e.kind, "question") == 0 ? 1 : strcmp(e.kind, "auth") == 0 ? 2 : 0;
      if (kindRank != rank) {
        continue;
      }

      panes[paneCount++] = i;
      if (rank == 1) {
        out[n++] = {255, 150, 0, 900, 400};
      } else if (rank == 2) {
        out[n++] = {255, 70, 0, 900, 400};
      } else if (ledStyle.doubleAfterMin >= 0 && millis() - e.since >= ledStyle.doubleAfterMin * 60000UL) {
        out[n++] = {255, 0, 0, 130, 120};
        out[n++] = {255, 0, 0, 130, 450};
      } else {
        out[n++] = {255, 0, 0, 300, 400};
      }
    }
  }

  // A pause before the next round
  if (n > 0) {
    out[n - 1].after = 1600;
  }
  return n;
}

void updateLed() {
  static Pulse pattern[8];
  static int pulses = 0;
  static unsigned long cycleStart = 0, cycleMs = 0;
  unsigned long now = millis();

  // The pattern changes only between rounds, so no pulse breaks off midway.
  if (now - cycleStart >= cycleMs) {
    pulses = blockedPattern(pattern);
    cycleStart = now;
    cycleMs = 0;
    for (int i = 0; i < pulses; i++) {
      cycleMs += pattern[i].on + pattern[i].after;
    }
  }

  if (pulses > 0) {
    unsigned long t = now - cycleStart;
    for (int i = 0; i < pulses; i++) {
      const Pulse &p = pattern[i];
      if (t < p.on + p.after) {
        setLevel(p.r, p.g, p.b, bump(t, p.on));
        return;
      }
      t -= p.on + p.after;
    }
  }

  int best = STATE_COUNT;
  for (auto &e : entries) {
    if (e.used && !isStale(e.source) && e.state < best && !(e.state == DONE && ledStyle.doneGlows)) {
      best = e.state;
    }
  }

  // Up quickly, held for a moment, then down slowly
  unsigned long sinceDone = now - doneAt;
  if (best != ERROR_ && ledStyle.doneGlows && doneAt != 0 && sinceDone < 1600) {
    float v = sinceDone < 200 ? sinceDone / 200.0f : sinceDone < 1100 ? 1 : (1600 - sinceDone) / 500.0f;
    setLevel(0, 120, 0, v);
    return;
  }

  switch (best) {
    case ERROR_:  setLed(120, 0, 120); break;
    case DONE:    setLed(0, 60, 0); break;
    case WORKING:
      if (ledStyle.workingBreathes) {
        setLevel(0, 0, 70, 0.25f + 0.75f * (0.5f - 0.5f * cosf(2 * PI * now / 4000)));
      } else {
        setLed(0, 0, 25);
      }
      break;
    default:      setLed(0, 0, 0);
  }
}

// Coverage 0..15 of one pixel of a 4-bit bitmap
uint8_t level(const Bitmap4 &b, int x, int y) {
  uint8_t v = b.px[y * ((b.w + 1) / 2) + x / 2];
  return x & 1 ? v & 0x0F : v >> 4;
}

// Draws a 4-bit bitmap in white over a plain background of r, g, b.
void drawBlended(const Bitmap4 &bmp, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  for (int yy = 0; yy < bmp.h; yy++) {
    for (int xx = 0; xx < bmp.w; xx++) {
      uint8_t a = level(bmp, xx, yy);
      if (a > 0) {
        canvas.drawPixel(x + xx, y + yy, canvas.color565(r + (255 - r) * a / 15, g + (255 - g) * a / 15, b + (255 - b) * a / 15));
      }
    }
  }
}

// Sends the canvas's top h lines to the screen at y, unless the slot already
//  shows exactly that. The hash covers the pixels and where they go.
void sendSlot(int slot, int y, int h) {
  const uint16_t *px = (const uint16_t *)canvas.getPointer();
  if (!px) {
    return;
  }

  uint32_t hash = 2166136261u ^ (y << 8 | h);
  for (int i = 0; i < SCREEN_W * h; i++) {
    hash = (hash ^ px[i]) * 16777619u;
  }
  hash = hash ? hash : 1;
  if (hash == shownSlots[slot]) {
    return;
  }

  shownSlots[slot] = hash;
  canvas.pushSprite(0, y, 0, 0, SCREEN_W, h);
}

// Clears the whole screen for a screen of its own, and forgets what the slots
//  showed, so the list comes back in full after it.
void clearScreen() {
  tft.fillScreen(TFT_BLACK);
  memset(shownSlots, 0, sizeof(shownSlots));
  rexShown = false;
}

// Draws wobbling Rex's drawing `n` in his canvas: the splash strokes with each
//  point moved a little, always the same way for the same n.
void drawWobbleRex(uint32_t n) {
  const float s = SPLASH_REX_H / REX_H, offset = REX_PEN * s / 2, margin = WOBBLE_PX + 3;
  uint32_t seed = n * 2654435761u + 1;
  auto wobble = [&]() {
    seed = seed * 1664525 + 1013904223;
    return ((seed >> 8) / 16777216.0f * 2 - 1) * WOBBLE_PX;
  };
  wobbleCanvas.fillSprite(0);

  for (const Stroke &st : STROKES) {
    float px = 0, py = 0;
    for (int i = 0; i < st.n; i++) {
      float x = margin + st.pts[i][0] * s + wobble(), y = margin + st.pts[i][1] * s + wobble();
      if (i > 0) {
        int steps = max(1, (int)ceilf(hypotf(x - px, y - py) / 0.5f));
        for (int k = 0; k <= steps; k++) {
          wobbleCanvas.fillRect(lroundf(px + (x - px) * k / steps - offset), lroundf(py + (y - py) * k / steps - offset), 2, 2, 1);
        }
      }
      px = x;
      py = y;
    }
  }
}

// Moves the calm screen's Rex on to his next drawing when it's time.
void wobbleRex() {
  static uint32_t shown = UINT32_MAX;
  if (!rexShown) {
    shown = UINT32_MAX;
    return;
  }

  uint32_t n = millis() / WOBBLE_MS % WOBBLE_DRAWINGS;
  if (n != shown) {
    shown = n;
    drawWobbleRex(n);
    wobbleCanvas.pushSprite(WOBBLE_X, WOBBLE_Y);
  }
}

// The grey bar at the top, with Rex and a title, drawn in the canvas
void drawBar(const String &title) {
  canvas.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_DARKGREY);
  // TFT_DARKGREY is 123, 125, 123
  drawBlended(HEADER_REX, 3, (HEADER_H - HEADER_REX.h) / 2, 123, 125, 123);
  canvas.setTextColor(TFT_WHITE, TFT_DARKGREY);
  canvas.setTextDatum(ML_DATUM);
  canvas.drawString(title, 3 + HEADER_REX.w + 5, HEADER_H / 2, 2);
}

// The bar alone, for the Wi-Fi screens
void showBar(const String &title) {
  drawBar(title);
  sendSlot(0, 0, HEADER_H);
}

// Text cut to fit `width` pixels in `font`
String fitText(String text, int width, int font) {
  while (text.length() > 0 && canvas.textWidth(text, font) > width) {
    text.remove(text.length() - 1);
  }
  return text;
}

// The row-size button: a little list of the rows a tap switches to, like the
//  page buttons that show where they go. Four close rows while the rows are
//  finger-sized, two wide ones while they are compact.
void drawRowsToggle() {
  int x = SCREEN_W - TOGGLE_W + 6, w = TOGGLE_W - 9;
  canvas.fillRoundRect(x, 2, w, HEADER_H - 4, 4, COLOR_BUTTON);
  bool toCompact = !compactRows;
  int lines = toCompact ? 4 : 2, gap = toCompact ? 4 : 8, thick = toCompact ? 2 : 4;
  int top = HEADER_H / 2 - ((lines - 1) * gap + thick) / 2;
  for (int i = 0; i < lines; i++) {
    int y = top + i * gap;
    canvas.fillRect(x + 5, y, 2, thick, i == 0 ? TFT_RED : TFT_BLUE);
    canvas.fillRect(x + 9, y + thick / 2 - (toCompact ? 1 : 0), w - 14, toCompact ? 1 : 2, TFT_WHITE);
  }
}

// Fills order with the indices of used entries, most urgent first, one per
//  pane, and returns how many. A program can report several records (opencode
//  sends one for the pane and one for its chat session), which would otherwise
//  show as duplicate rows.
int panesByUrgency(int *order) {
  int count = 0;
  for (int s = 0; s < STATE_COUNT; s++) {
    for (int i = 0; i < MAX_ENTRIES; i++) {
      if (entries[i].used && entries[i].state == s && !listed(order, count, entries[i])) {
        order[count++] = i;
      }
    }
  }

  // Among blocked rows, the one that has waited longest comes first.
  unsigned long now = millis();
  int blocked = 0;
  while (blocked < count && entries[order[blocked]].state == BLOCKED) {
    blocked++;
  }
  for (int i = 1; i < blocked; i++) {
    int idx = order[i], j = i;
    for (; j > 0 && now - entries[order[j - 1]].since < now - entries[idx].since; j--) {
      order[j] = order[j - 1];
    }
    order[j] = idx;
  }
  return count;
}

// Panes per state, leaving out stale sources, as the LED does. Returns the total.
int countLive(const int *order, int count, int *counts) {
  int total = 0;
  memset(counts, 0, STATE_COUNT * sizeof(counts[0]));
  for (int i = 0; i < count; i++) {
    const Entry &e = entries[order[i]];
    if (!isStale(e.source)) {
      counts[e.state]++;
      total++;
    }
  }
  return total;
}

// The calm screen instead of the list: nothing waits, and no tap asked for the list.
bool calmScreen(const int *counts, int count) {
  return count == 0 || (calmEnabled && counts[BLOCKED] == 0 && listUntil == 0);
}

const char *countName(int state) {
  return state == BLOCKED ? "waiting" : stateNames[state];
}

// The counts in the header, from x within `room`: a chip in the state's color
//  and "2 waiting" for each state there is, or the numbers alone when the
//  words don't fit.
void drawSummary(const int *counts, int x, int room) {
  for (int words = 1; words >= 0; words--) {
    int w = 0;
    for (int s = 0; s < STATE_COUNT; s++) {
      if (counts[s]) {
        String text = words ? String(counts[s]) + " " + countName(s) : String(counts[s]);
        w += 7 + canvas.textWidth(text, 2) + 10;
      }
    }
    if (w - 10 > room && words) {
      continue;
    }

    canvas.setTextColor(TFT_WHITE, TFT_DARKGREY);
    canvas.setTextDatum(ML_DATUM);
    for (int s = 0; s < STATE_COUNT; s++) {
      if (!counts[s]) {
        continue;
      }

      // The idle chip's grey is the bar's own, so it takes a lighter one.
      canvas.fillRect(x, 6, 4, HEADER_H - 12, s == IDLE ? TFT_LIGHTGREY : stateColors[s]);
      String text = words ? String(counts[s]) + " " + countName(s) : String(counts[s]);
      canvas.drawString(text, x + 7, HEADER_H / 2, 2);
      x += 7 + canvas.textWidth(text, 2) + 10;
    }
    return;
  }
}

void drawHeader() {
  // Sources left of the button: white while live, black when stale. The title
  //  gets the room left of them.
  int sourcesW = 0;
  for (int i = 0; i < MAX_SOURCES; i++) {
    if (sources[i].used) {
      sourcesW += canvas.textWidth(sources[i].name, 2) + 8;
    }
  }
  String title = WiFi.status() == WL_CONNECTED ? String(HOSTNAME ".local ") + WiFi.localIP().toString() : String("Wi-Fi connecting...");
  int room = SCREEN_W - TOGGLE_W - 4 - sourcesW - (3 + HEADER_REX.w + 5);
  // With many sources the address alone, which is the part to type somewhere.
  if (canvas.textWidth(title, 2) > room && WiFi.status() == WL_CONNECTED) {
    title = WiFi.localIP().toString();
  }
  // Over the list the title gives way to the counts. The calm screen has them
  //  large, so it keeps the address.
  int order[MAX_ENTRIES], counts[STATE_COUNT];
  int count = panesByUrgency(order);
  bool summary = headerCounts && countLive(order, count, counts) > 0 && !calmScreen(counts, count);
  drawBar(summary ? String() : fitText(title, room, 2));
  if (summary) {
    drawSummary(counts, 3 + HEADER_REX.w + 5, room);
  }
  drawRowsToggle();

  int x = SCREEN_W - TOGGLE_W - 4;
  canvas.setTextDatum(MR_DATUM);

  for (int i = MAX_SOURCES - 1; i >= 0; i--) {
    if (!sources[i].used) {
      continue;
    }

    bool stale = isStale(sources[i].name);
    canvas.setTextColor(stale ? TFT_BLACK : TFT_WHITE, TFT_DARKGREY);
    canvas.drawString(sources[i].name, x, HEADER_H / 2, 2);
    x -= canvas.textWidth(sources[i].name, 2) + 8;
  }
  sendSlot(0, 0, HEADER_H);
}

// Length of the pane part of a key, before the '#'
size_t paneLength(const char *key) {
  const char *hash = strchr(key, '#');
  return hash ? hash - key : strlen(key);
}

bool samePane(const Entry &a, const Entry &b) {
  size_t length = paneLength(a.key);
  return strcmp(a.source, b.source) == 0
      && length == paneLength(b.key)
      && strncmp(a.key, b.key, length) == 0;
}

bool listed(const int *order, int count, const Entry &e) {
  for (int i = 0; i < count; i++) {
    if (samePane(entries[order[i]], e)) {
      return true;
    }
  }
  return false;
}

// How long a row has been in its state: "4m", "1h 12m", "2d 3h", or "" under a minute.
void formatAge(unsigned long ms, char *out, size_t size) {
  unsigned long m = ms / 60000;
  if (m == 0) {
    out[0] = 0;
  } else if (m < 60) {
    snprintf(out, size, "%lum", m);
  } else if (m < 24 * 60) {
    snprintf(out, size, "%luh %lum", m / 60, m % 60);
  } else {
    snprintf(out, size, "%lud %luh", m / (24 * 60), m / 60 % 24);
  }
}

int rowHeight() {
  return compactRows ? ROW_H_COMPACT : ROW_H_FINGER;
}

// Draws drawn row r for entry e in the canvas and sends it, so one row can
//  change alone. A compact row is one line. A finger-sized row gives the message a second line
//  of its own, and a divider below, so the tap targets are easy to tell apart.
void drawRow(int r, const Entry &e) {
  int h = rowHeight();
  int y = 0;
  int top = compactRows ? y + h / 2 : y + 13;
  bool stale = isStale(e.source);
  uint16_t chip = stale ? COLOR_GREY : stateColors[e.state];
  uint16_t text = stale ? COLOR_GREY : TFT_WHITE;

  canvas.fillRect(0, y, SCREEN_W, h, TFT_BLACK);
  canvas.fillRect(0, y + 2, 6, h - 4, chip);
  if (!compactRows) {
    canvas.drawFastHLine(10, y + h - 1, SCREEN_W - 10, COLOR_DIVIDER);
  }
  canvas.setTextDatum(ML_DATUM);
  canvas.setTextColor(chip, TFT_BLACK);
  String label = e.state == BLOCKED && e.kind[0] ? String(e.kind) : String(stateNames[e.state]);
  canvas.drawString(label, 10, top, 2);

  // The time sits at the right edge of the first line.
  char *age = drawnRows[r].age;
  formatAge(millis() - e.since, age, sizeof(drawnRows[r].age));
  int right = SCREEN_W - 2;
  if (age[0]) {
    canvas.setTextColor(COLOR_GREY, TFT_BLACK);
    canvas.setTextDatum(MR_DATUM);
    canvas.drawString(age, SCREEN_W - 3, top, 2);
    canvas.setTextDatum(ML_DATUM);
    right = SCREEN_W - 3 - canvas.textWidth(age, 2) - 8;
  }

  String who = String(e.session) + " " + e.app;
  canvas.setTextColor(text, TFT_BLACK);
  canvas.drawString(fitText(who, right - 86, 2), 86, top, 2);

  // The message gets what is left, cut to fit.
  canvas.setTextColor(COLOR_GREY, TFT_BLACK);
  if (compactRows) {
    int msgX = 86 + canvas.textWidth(who + "  ", 2);
    canvas.drawString(fitText(e.msg, right - msgX, 2), msgX, top, 2);
  } else if (e.msg[0]) {
    canvas.drawString(fitText(e.msg, SCREEN_W - 13, 2), 10, y + 31, 2);
  } else {
    // A dash for no message, so every row keeps the same two lines.
    canvas.drawFastHLine(10, y + 31, 12, COLOR_GREY);
  }
  sendSlot(r + 1, HEADER_H + r * h, h);
}

// A button that turns the page, with an arrow. Dimmed at either end.
void drawPageButton(int x, int y, int h, bool next, const String &label, bool enabled) {
  uint16_t color = enabled ? TFT_WHITE : COLOR_DIVIDER;
  canvas.fillRoundRect(x, y, PAGE_BUTTON_W, h, 6, enabled ? COLOR_BUTTON : TFT_BLACK);
  canvas.drawRoundRect(x, y, PAGE_BUTTON_W, h, 6, enabled ? COLOR_GREY : COLOR_DIVIDER);
  int mid = y + h / 2, tip = next ? x + PAGE_BUTTON_W - 10 : x + 10, back = next ? tip - 6 : tip + 6;
  canvas.fillTriangle(tip, mid, back, mid - 6, back, mid + 6, color);
  canvas.setTextColor(color, enabled ? COLOR_BUTTON : TFT_BLACK);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString(label, x + PAGE_BUTTON_W / 2 + (next ? -6 : 6), mid, 2);
}

// Keeps the list up while an agent waits and for LIST_MS after the last one
//  stops, when the calm screen's quiet time starts. Runs before each redraw.
void trackWaiting() {
  int order[MAX_ENTRIES], counts[STATE_COUNT];
  countLive(order, panesByUrgency(order), counts);
  static bool waiting = false;
  if (waiting && counts[BLOCKED] == 0) {
    quietSince = millis();
  }
  if (waiting || counts[BLOCKED] > 0) {
    listUntil = millis() + LIST_MS;
  }
  waiting = counts[BLOCKED] > 0;
}

// How long nothing has waited, for the calm screen
void quietText(char *out, size_t size) {
  char age[8];
  formatAge(millis() - quietSince, age, sizeof(age));
  if (quietSince == 0) {
    strlcpy(out, "Nothing has waited yet", size);
  } else if (!age[0]) {
    strlcpy(out, "Quiet for under a minute", size);
  } else {
    snprintf(out, size, "Quiet for %s", age);
  }
}

// The counts centred at y in the canvas, each in its state's color, with grey
//  dots between. Font 4 when it fits, so it reads across the desk.
void drawCounts(const int *counts, int y) {
  int font = 4;
  for (;;) {
    int w = 0;
    for (int s = 0; s < STATE_COUNT; s++) {
      if (counts[s]) {
        w += canvas.textWidth(String(counts[s]) + " " + countName(s), font) + 16;
      }
    }
    w -= 16;
    if (w > SCREEN_W - 16 && font == 4) {
      font = 2;
      continue;
    }

    int x = (SCREEN_W - w) / 2;
    bool first = true;
    canvas.setTextDatum(ML_DATUM);
    for (int s = 0; s < STATE_COUNT; s++) {
      if (!counts[s]) {
        continue;
      }

      if (!first) {
        canvas.fillCircle(x - 8, y, 2, COLOR_GREY);
      }
      first = false;
      String text = String(counts[s]) + " " + countName(s);
      canvas.setTextColor(stateColors[s], TFT_BLACK);
      canvas.drawString(text, x, y, font);
      x += canvas.textWidth(text, font) + 16;
    }
    return;
  }
}

void drawRows() {
  int order[MAX_ENTRIES];
  int count = panesByUrgency(order);
  int counts[STATE_COUNT];
  int live = countLive(order, count, counts);

  int h = rowHeight();
  int slots = (SCREEN_H - HEADER_H) / h;
  // When the rows change size, every slot moves, and the lines below the last
  //  slot are left from the other size.
  static int shownH = 0;
  if (h != shownH) {
    shownH = h;
    memset(shownSlots + 1, 0, MAX_SLOTS * sizeof(shownSlots[0]));
    tft.fillRect(0, HEADER_H + slots * h, SCREEN_W, SCREEN_H - HEADER_H - slots * h, TFT_BLACK);
  }
  drawnCount = 0;
  moreSlot = -1;
  if (calmScreen(counts, count)) {
    page = 0;
    shownQuiet[0] = 0;
    if (live > 0) {
      quietText(shownQuiet, sizeof(shownQuiet));
    }
    // The lines span slots: each draws them shifted up by its own place, and
    //  the canvas cuts off the rest.
    for (int s = 0; s < slots; s++) {
      int y = -HEADER_H - s * h;
      canvas.fillRect(0, 0, SCREEN_W, h, TFT_BLACK);
      canvas.setTextColor(COLOR_GREY, TFT_BLACK);
      canvas.setTextDatum(MC_DATUM);
      if (live > 0) {
        drawCounts(counts, CALM_COUNTS_Y + y);
        canvas.setTextColor(COLOR_GREY, TFT_BLACK);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(shownQuiet, SCREEN_W / 2, CALM_QUIET_Y + y, 2);
      } else {
        canvas.drawString(count == 0 ? "No agent status yet" : "No live agent status", SCREEN_W / 2, CALM_COUNTS_Y + y, 2);
      }
      sendSlot(s + 1, HEADER_H + s * h, h);
    }
    // A slot sent just now may have cut through him.
    wobbleCanvas.pushSprite(WOBBLE_X, WOBBLE_Y);
    rexShown = true;
    return;
  }

  // Slots left blank under the rows would otherwise keep pieces of Rex.
  if (rexShown) {
    rexShown = false;
    memset(shownSlots + 1, 0, MAX_SLOTS * sizeof(shownSlots[0]));
  }

  // The last slot is preserved for pagination for when the rows don't fit
  // The first page holds the most urgent ones.
  int perPage = count > slots ? slots - 1 : slots;
  int pages = (count + perPage - 1) / perPage;
  pageCount = pages;
  if (page >= pages) {
    page = pages - 1;
  }

  int first = page * perPage;

  for (int r = 0; r < perPage && first + r < count; r++) {
    const Entry &e = entries[order[first + r]];
    strlcpy(drawnRows[r].source, e.source, sizeof(drawnRows[r].source));
    strlcpy(drawnRows[r].key, e.key, sizeof(drawnRows[r].key));

    drawnRows[r].entry = order[first + r];
    drawnCount = r + 1;

    drawRow(r, e);
  }

  // Slots under the last row are blank.
  for (int r = drawnCount; r < (pages > 1 ? slots - 1 : slots); r++) {
    canvas.fillRect(0, 0, SCREEN_W, h, TFT_BLACK);
    sendSlot(r + 1, HEADER_H + r * h, h);
  }

  if (pages > 1) {
    moreSlot = slots - 1;
    int y = 0;
    int after = count - first - perPage;
    canvas.fillRect(0, 0, SCREEN_W, h, TFT_BLACK);

    drawPageButton(6, y + 3, h - 6, false, String("Back"), page > 0);
    drawPageButton(SCREEN_W - 6 - PAGE_BUTTON_W, y + 3, h - 6, true, after > 0 ? String(after) + " more" : String("More"), after > 0);

    canvas.setTextColor(COLOR_GREY, TFT_BLACK);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(String(page + 1) + " / " + pages, SCREEN_W / 2, y + h / 2, 2);
    sendSlot(moreSlot + 1, HEADER_H + moreSlot * h, h);
  }
}

// Times change with the clock alone. Checked once a second, and only rows
//  whose text changed are redrawn, so the list doesn't flicker every minute.
void refreshAges() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) {
    return;
  }

  lastCheck = millis();
  if (rexShown && shownQuiet[0]) {
    char quiet[sizeof(shownQuiet)];
    quietText(quiet, sizeof(quiet));
    if (strcmp(quiet, shownQuiet) != 0) {
      dirty = true;
    }
  }

  char age[8];
  for (int r = 0; r < drawnCount; r++) {
    const Entry &e = entries[drawnRows[r].entry];
    formatAge(millis() - e.since, age, sizeof(age));
    if (strcmp(age, drawnRows[r].age) != 0) {
      drawRow(r, e);
    }
  }
}

// Follows presses on the touch screen. Returns true once when one ends, with
//  where it was in screen pixels, at the average of its settled readings.
bool pressEnded(int &x, int &y) {
  static unsigned long pressStarted = 0, lastSeen = 0;
  static long sumX = 0, sumY = 0;
  static int readings = 0;
  unsigned long now = millis();

  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    if (pressStarted == 0) {
      pressStarted = now;
      sumX = sumY = readings = 0;
    }
    lastSeen = now;
    // On lift-off the library reports z 0 with the previous coordinates.
    if (p.z > 0 && now - pressStarted >= TOUCH_SETTLE_MS) {
      sumX += p.x;
      sumY += p.y;
      readings++;
    }
    return false;
  }

  if (pressStarted == 0 || now - lastSeen < TOUCH_RELEASE_MS) {
    return false;
  }

  pressStarted = 0;
  if (readings == 0) {
    return false;
  }

  x = constrain(map(sumX / readings, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W - 1), 0, SCREEN_W - 1);
  y = constrain(map(sumY / readings, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
  return true;
}

void setCompactRows(bool compact) {
  compactRows = compact;
  Preferences settings;
  settings.begin(SETTINGS_NAMESPACE, false);
  settings.putBool(KEY_ROWS_COMPACT, compact);
  settings.end();
  Serial.printf("Rows: %s\n", compact ? "compact" : "finger-sized");
}

// Sends a tap when a press on a row ends. The button at the right of the header
//  switches between finger-sized and compact rows; the last slot turns pages.
void checkTouch() {
  int x, y;
  if (!pressEnded(x, y)) {
    return;
  }

  if (y < HEADER_H) {
    if (x < SCREEN_W - TOGGLE_W) {
      return;
    }

    setCompactRows(!compactRows);

    page = 0;
    dirty = true;
    return;
  }

  // Any tap below the header keeps the list up for another LIST_MS, and a tap
  //  on the calm screen brings it up.
  listUntil = millis() + LIST_MS;
  if (rexShown) {
    dirty = true;
    return;
  }

  int h = rowHeight();
  int r = (y - HEADER_H) / h;
  if (r == moreSlot) {
    // Left half back, right half on; at either end that half does nothing.
    int to = x < SCREEN_W / 2 ? page - 1 : page + 1;
    if (to >= 0 && to < pageCount) {
      page = to;
      pageAt = millis();
      dirty = true;
    }

    return;
  }
  if (r >= drawnCount) {
    return;
  }
  pageAt = millis();

  // Outline the row: white when a watcher took the tap, grey when none listens.
  bool sent = sendTap(drawnRows[r]);
  Serial.printf("Tap row %d %s %s: %s\n", r, drawnRows[r].source, drawnRows[r].key, sent ? "sent" : "no watcher");
  tft.drawRect(0, HEADER_H + r * h, SCREEN_W, h, sent ? TFT_WHITE : COLOR_GREY);
  shownSlots[r + 1] = 0;
  holdUntil = millis() + TAP_FLASH_MS;
  dirty = true;
}

// Waits up to `ms` for a tap and says where it landed.
bool waitForTap(int &x, int &y, unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    improvPoll();
    if (pressEnded(x, y)) {
      return true;
    }
    delay(5);
  }
  return false;
}

// Waits until the screen has been let go for a moment. A resting finger can
//  read as lifted for a few ms, which mustn't count as the next tap.
void waitForRelease() {
  unsigned long seen = millis();
  while (millis() - seen < 200) {
    if (touch.touched()) {
      seen = millis();
    }
    improvPoll();
    delay(5);
  }
}

void drawButton(int x, int y, int w, int h, const char *label, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 8, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, color);
  tft.drawString(label, x + w / 2, y + h / 2, 4);
}

// A screen with a title and a line below it, for Wi-Fi steps that take a moment
void drawNotice(const String &title, const String &line) {
  clearScreen();
  showBar("Wi-Fi");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(fitText(title, SCREEN_W - 16, 4), SCREEN_W / 2, 110, 4);
  tft.setTextColor(COLOR_GREY, TFT_BLACK);
  tft.drawString(fitText(line, SCREEN_W - 16, 2), SCREEN_W / 2, 142, 2);
}

// Never returns.
void restartBoard() {
  Serial.println("Restarting");
  Serial.flush();
  delay(500);
  ESP.restart();
}

// Reads the saved network into wifiSsid and wifiPassword. False if there is none.
bool loadSavedWifi() {
  Preferences settings;
  // Opening read-only fails while nothing was ever saved.
  if (!settings.begin(SETTINGS_NAMESPACE, true)) {
    return false;
  }

  String ssid = settings.isKey(KEY_WIFI_SSID) ? settings.getString(KEY_WIFI_SSID) : String();
  String password = settings.isKey(KEY_WIFI_PASSWORD) ? settings.getString(KEY_WIFI_PASSWORD) : String();
  settings.end();
  if (ssid.isEmpty()) {
    return false;
  }

  wifiSsid = ssid;
  wifiPassword = password;
  return true;
}

void loadRowStyle() {
  Preferences settings;
  // Opening read-only fails while nothing was ever saved.
  if (settings.begin(SETTINGS_NAMESPACE, true)) {
    compactRows = settings.getBool(KEY_ROWS_COMPACT, false);
    headerCounts = settings.getBool(KEY_HEADER_COUNTS, true);
    calmEnabled = settings.getBool(KEY_CALM_SCREEN, true);
    settings.end();
  }
}

void saveWifi(const String &ssid, const String &password) {
  Preferences settings;
  settings.begin(SETTINGS_NAMESPACE, false);
  settings.putString(KEY_WIFI_SSID, ssid);
  settings.putString(KEY_WIFI_PASSWORD, password);
  settings.end();
  Serial.printf("Wi-Fi: saved %s\n", ssid.c_str());
}

// Removes the saved network, and the copy that builds before this one left in
//  the Wi-Fi driver's own storage. The other settings stay.
void forgetWifi() {
  Preferences settings;
  settings.begin(SETTINGS_NAMESPACE, false);
  settings.remove(KEY_WIFI_SSID);
  settings.remove(KEY_WIFI_PASSWORD);
  settings.end();
  WiFi.disconnect(true, true);
  Serial.println("Wi-Fi: forgot the saved network");
}

// Picks the network to join, in the order at the top of this file.
void chooseWifi() {
  if (loadSavedWifi()) {
    wifiFrom = WIFI_SAVED;
    return;
  }

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  wifiSsid = WIFI_SSID;
  wifiPassword = WIFI_PASSWORD;
  wifiFrom = WIFI_SECRETS;
#endif
}

// Starts joining the network in use. Auto-reconnect and loop() keep at it.
void beginWifi() {
  Serial.printf("Wi-Fi: joining %s (%s)\n", wifiSsid.c_str(), wifiFrom == WIFI_SAVED ? "saved" : "secrets.h");
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
}

// Joins a network picked on the setup page or sent by Improv,
//  and saves it if that works. Blocks for up to JOIN_MS.
// On failure it leaves the radio off that network,
//  says so on the setup screen and sets screenTaken.
// What to join next is up to the caller
bool joinNetwork(const String &ssid, const String &password) {
  Serial.printf("Wi-Fi: trying %s\n", ssid.c_str());

  drawNotice("Joining", ssid);

  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis(), lastBegin = start;
  while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_MS) {
    // Without auto-reconnect, as in setup mode, a failed attempt stays failed.
    wl_status_t status = WiFi.status();
    if ((status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) && millis() - lastBegin > 5000) {
      lastBegin = millis();
      WiFi.begin(ssid.c_str(), password.c_str());
    }
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi: couldn't join %s\n", ssid.c_str());
    WiFi.disconnect();
    setupNote = String("Couldn't join ") + ssid + ". Try again.";
    screenTaken = true;
    return false;
  }

  saveWifi(ssid, password);
  drawNotice("Joined", ssid);
  return true;
}

// What Improv needs from the board, see improv_serial.h

bool improvOnline() {
  return WiFi.status() == WL_CONNECTED;
}

String improvUrl() {
  return String("http://") + WiFi.localIP().toString() + "/settings";
}

bool improvJoin(const String &ssid, const String &password) {
  if (joinNetwork(ssid, password)) {
    return true;
  }

  // Back to the network in use. Setup mode goes back to it by itself.
  if (!inSetup && wifiFrom != WIFI_NONE) {
    beginWifi();
  }
  return false;
}

// It restarts once joined, like the setup page does.
const ImprovDevice improvDevice = {
  FIRMWARE_NAME, FIRMWARE_VERSION, "ESP32", "Cheap Rex Display",
  improvOnline, improvUrl, improvJoin, restartBoard,
};

// A new password each time the hotspot opens, from letters that can't be taken
//  for one another. esp_random() is truly random while the radio is on.
void makeHotspotPassword() {
  static const char letters[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < HOTSPOT_PASSWORD_LEN; i++) {
    hotspotPassword[i] = letters[esp_random() % (sizeof(letters) - 1)];
  }
  hotspotPassword[HOTSPOT_PASSWORD_LEN] = 0;
}

// Fills `networks` with the ones in range, strongest first, each name once.
//  Hidden networks have no name to pick, so they are left out.
void scanForPortal() {
  int found = WiFi.scanNetworks();
  networkCount = 0;

  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    SeenNetwork *same = nullptr;
    for (int k = 0; k < networkCount; k++) {
      if (networks[k].ssid == ssid) {
        same = &networks[k];
      }
    }

    if (ssid.isEmpty()) {
      continue;
    }

    if (same) {
      same->rssi = max(same->rssi, (int)WiFi.RSSI(i));
    } else if (networkCount < MAX_NETWORKS) {
      networks[networkCount++] = {ssid, (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN};
    }
  }

  WiFi.scanDelete();
  std::sort(networks, networks + networkCount, [](const SeenNetwork &a, const SeenNetwork &b) { return a.rssi > b.rssi; });
  Serial.printf("Setup: %d networks in range\n", networkCount);
}

String htmlEscape(const String &text) {
  String out;
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:  out += c;
    }
  }
  return out;
}

const char PAGE_START[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Cheap Rex Display setup</title>"
    "<style>body{font:17px system-ui,sans-serif;max-width:400px;margin:24px auto;padding:0 16px}"
    "label{display:block;margin:16px 0 6px}"
    "select,input,button{font:inherit;width:100%;padding:10px;box-sizing:border-box}"
    "button{margin-top:20px}.problem{color:#c00}</style>"
    "<h1>Wi-Fi setup</h1>";

// The setup page: the networks in range, a password and Save
String portalPage(const String &problem) {
  String html = PAGE_START;
  if (!problem.isEmpty()) {
    html += String("<p class=problem>") + htmlEscape(problem) + "</p>";
  }

  if (networkCount == 0) {
    html += "<p>No networks in range.</p>";
  } else {
    html += "<form method=post action=/save><label for=n>Network</label><select id=n name=ssid>";
    for (int i = 0; i < networkCount; i++) {
      String name = htmlEscape(networks[i].ssid);
      html += String("<option value=\"") + name + "\"";
      html += networks[i].ssid == wifiSsid ? " selected>" : ">";
      html += name;
      html += networks[i].open ? " (open)</option>" : "</option>";
    }
    html += "</select><label for=p>Password</label><input id=p name=password type=password>"
            "<button>Save</button></form>";
  }

  html += "<p>The board only sees 2.4 GHz networks. <a href=/scan>Scan again</a></p>";
  return html;
}

void handlePortalPage() {
  portal.send(200, "text/html", portalPage(""));
}

// Every name points at the board, so phones checking for a captive portal get
//  sent here, see that it isn't their answer and open the page.
void handlePortalRedirect() {
  portal.sendHeader("Location", String("http://") + HOTSPOT_IP.toString() + "/");
  portal.send(302, "text/plain", "");
}

void handlePortalScan() {
  scanForPortal();
  handlePortalRedirect();
}

// Takes the network picked on the page. The join waits until the reply is out:
//  it moves the radio to that network's channel, which can drop the phone.
void handlePortalSave() {
  String ssid = portal.arg("ssid"), password = portal.arg("password");
  String problem = ssid.isEmpty() || ssid.length() > 32 ? "Pick a network."
      : password.length() > 0 && password.length() < 8 ? "Wi-Fi passwords have at least 8 characters."
      : password.length() > 64 ? "That password is too long."
      : "";
  if (!problem.isEmpty()) {
    portal.send(400, "text/html", portalPage(problem));
    return;
  }

  String html = PAGE_START;
  html += String("<p>Joining <b>") + htmlEscape(ssid) + "</b>.</p>"
          "<p>Watch the board's screen. Once it's on, it restarts and this hotspot goes away.</p>"
          "<p>If the screen says it couldn't join, join <b>";
  html += hotspotName;
  html += "</b> again and try once more.</p>";
  portal.send(200, "text/html", html);
  pendingSsid = ssid;
  pendingPassword = password;
}

// Joins the network picked on the page. It restarts when that works; otherwise
//  the hotspot stays and the screen says what happened.
void joinFromPortal() {
  String ssid = pendingSsid, password = pendingPassword;
  pendingSsid = pendingPassword = "";
  delay(500);  // for the reply to reach the phone
  if (joinNetwork(ssid, password)) {
    restartBoard();
  }
}

// Keeps trying the network in use while no phone is on the hotspot, so after a
//  router restart the board finds its way back by itself. A try moves the radio
//  to that network's channel and would drop a phone, hence the wait.
void retryNetworkInUse() {
  static unsigned long lastTry = millis();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Setup: %s is back\n", wifiSsid.c_str());
    restartBoard();
  }

  if (wifiFrom == WIFI_NONE || WiFi.softAPgetStationNum() > 0 || millis() - lastTry < REJOIN_MS) {
    return;
  }

  lastTry = millis();
  beginWifi();
}

// The setup page takes port 80, so the status server makes room.
void stopServers() {
  if (!serving) {
    return;
  }

  for (auto &t : tapClients) {
    t.client.stop();
  }

  tapServer.end();
  server.stop();
  MDNS.end();
  serving = false;
}

// Opens the board's own Wi-Fi and the setup page on it.
// Every name resolves to the board (a captive portal),
//  so phones show the page as soon as they join.
void startHotspot() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  scanForPortal();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(hotspotName, sizeof(hotspotName), "crd-setup-%02X%02X", mac[4], mac[5]);
  makeHotspotPassword();
  WiFi.softAPConfig(HOTSPOT_IP, HOTSPOT_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(hotspotName, hotspotPassword);
  dns.start(53, "*", HOTSPOT_IP);

  portal.on("/", HTTP_GET, handlePortalPage);
  portal.on("/save", HTTP_POST, handlePortalSave);
  portal.on("/scan", HTTP_GET, handlePortalScan);
  portal.onNotFound(handlePortalRedirect);
  portal.begin();
  Serial.printf("Setup: hotspot %s, page at http://%s/\n", hotspotName, HOTSPOT_IP.toString().c_str());
}

void drawSetupScreen() {
  clearScreen();
  showBar("Wi-Fi setup");
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("1. On a phone or computer, join:", 8, 40, 2);
  tft.drawString(hotspotName, 100, 66, 4);
  tft.drawString(hotspotPassword, 100, 96, 4);
  tft.drawString("2. A page opens. If not, go to", 8, 126, 2);
  tft.drawString("http://192.168.4.1", 26, 144, 2);
  tft.drawString("3. Pick your network, enter its", 8, 166, 2);
  tft.drawString("password, then choose Save.", 26, 184, 2);

  tft.setTextColor(COLOR_GREY, TFT_BLACK);
  tft.drawString("Name", 26, 66, 2);
  tft.drawString("Password", 26, 96, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(fitText(setupNote, SCREEN_W - 16, 2), 8, 220, 2);
}

// Setup mode: the board's own hotspot, with a page to pick a network.
// It ends by restarting, once a network picked on the page or sent by Improv is joined
//  and saved, or once the network in use is back. Never returns.
void runSetup(const String &note) {
  Serial.println("Setup: opening the hotspot");
  inSetup = true;
  setupNote = note;
  stopServers();
  setLed(0, 0, 0);
  startHotspot();
  drawSetupScreen();

  for (;;) {
    dns.processNextRequest();
    portal.handleClient();
    improvPoll();
    if (!pendingSsid.isEmpty()) {
      joinFromPortal();
    }

    retryNetworkInUse();
    if (screenTaken) {
      screenTaken = false;
      drawSetupScreen();
    }
    delay(2);
  }
}

// After a long hold on the splash: asks whether to forget the saved network.
// Forgetting restarts the board, and anything else lets it carry on booting.
void offerForget() {
  const int buttonY = 130, buttonW = 130, buttonH = 56, keepX = 20, forgetX = 170;
  Serial.println("Wi-Fi: asking whether to forget the saved network");

  clearScreen();
  showBar("Wi-Fi");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Forget this Wi-Fi?", SCREEN_W / 2, 52, 4);
  tft.setTextColor(COLOR_GREY, TFT_BLACK);
  tft.drawString(fitText(wifiSsid, SCREEN_W - 16, 2), SCREEN_W / 2, 80, 2);
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  tft.drawString("Then it joins the one in secrets.h.", SCREEN_W / 2, 102, 2);
#else
  tft.drawString("Then it opens its setup hotspot.", SCREEN_W / 2, 102, 2);
#endif
  drawButton(keepX, buttonY, buttonW, buttonH, "Keep", TFT_DARKGREY);
  drawButton(forgetX, buttonY, buttonW, buttonH, "Forget", TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_GREY, TFT_BLACK);
  tft.drawString(String("It stays if you wait ") + FORGET_ASK_MS / 1000 + " s.", SCREEN_W / 2, 214, 2);

  // The finger that held the splash must not count as the answer.
  waitForRelease();
  int x, y;
  bool forget = waitForTap(x, y, FORGET_ASK_MS)
      && x >= forgetX && x < forgetX + buttonW && y >= buttonY && y < buttonY + buttonH;
  if (forget) {
    forgetWifi();
    drawNotice("Wi-Fi forgotten", "Restarting...");
    restartBoard();
  }

  Serial.println("Wi-Fi: kept the saved network");
  clearScreen();
}

unsigned long splashPressStart = 0, splashPressSeen = 0;  // the press on the splash, 0 when none

// Watches the screen during the splash. A press that ends before FORGET_HOLD_MS
//  is a tap and skips the splash. With a saved network, holding on that long
//  asks whether to forget it. Returns true when the splash should end.
bool splashInput() {
  improvPoll();
  if (screenTaken) {
    return true;  // an Improv join that failed drew over it
  }

  unsigned long now = millis();
  if (touch.touched()) {
    if (splashPressStart == 0) {
      splashPressStart = now;
    }
    splashPressSeen = now;

    if (wifiFrom == WIFI_SAVED && now - splashPressStart >= FORGET_HOLD_MS) {
      offerForget();
      splashPressStart = 0;
      return true;
    }
    return false;
  }

  if (splashPressStart == 0 || now - splashPressSeen < TOUCH_RELEASE_MS) {
    return false;
  }

  splashPressStart = 0;
  return true;
}

// The splash's bottom line: Wi-Fi progress, then the board's address. While a
//  finger rests on it with a saved network, how long until it offers to forget.
void splashStatus(unsigned long start) {
  static String shown;
  unsigned long held = splashPressStart ? millis() - splashPressStart : 0;
  String text;

  if (wifiFrom == WIFI_SAVED && held >= FORGET_HINT_MS && held < FORGET_HOLD_MS) {
    text = String("Hold ") + ((FORGET_HOLD_MS - held) / 1000 + 1) + " s more to forget Wi-Fi";
  } else if (wifiFrom == WIFI_NONE) {
    text = "No Wi-Fi saved yet, setup comes next";
  } else if (WiFi.status() == WL_CONNECTED) {
    text = String(HOSTNAME ".local ") + WiFi.localIP().toString();
  } else {
    text = String("Wi-Fi connecting") + String("...").substring(0, (millis() - start) / 333 % 3 + 1);
  }

  if (text == shown) {
    return;
  }

  shown = text;
  tft.fillRect(0, SCREEN_H - 22, SCREEN_W, 22, TFT_BLACK);
  tft.setTextColor(COLOR_GREY, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(text, 8, SCREEN_H - 12, 2);
}

// Rex's strokes taken as one path, `walk` is called for
//  each straight piece in screen pixels with its length.
template <typename F>
void walkRex(F walk) {
  const float s = SPLASH_REX_H / REX_H;
  for (const Stroke &st : STROKES) {
    for (int i = 1; i < st.n; i++) {
      float x0 = SPLASH_REX_X + st.pts[i - 1][0] * s, y0 = SPLASH_REX_Y + st.pts[i - 1][1] * s;
      float x1 = SPLASH_REX_X + st.pts[i][0] * s, y1 = SPLASH_REX_Y + st.pts[i][1] * s;
      walk(x0, y0, x1, y1, hypotf(x1 - x0, y1 - y0));
    }
  }
}

// Draws the stretch of Rex's path from length `from` to `to`,
//  stamping 2x2 pixels every half pixel along it.
void traceRex(float from, float to) {
  const float offset = REX_PEN * SPLASH_REX_H / REX_H / 2;
  float walked = 0;

  walkRex([&](float x0, float y0, float x1, float y1, float seg) {
    float a = max(from - walked, 0.0f), b = min(to - walked, seg);
    walked += seg;

    if (b <= a) {
      return;
    }

    int n = max(1, (int)ceilf((b - a) / 0.5f));
    for (int k = 0; k <= n; k++) {
      float d = a + (b - a) * k / n;
      tft.fillRect(lroundf(x0 + (x1 - x0) * d / seg - offset), lroundf(y0 + (y1 - y0) * d / seg - offset), 2, 2, TFT_WHITE);
    }
  });
}

// Draws columns [c0, c1) of a 1-bit bitmap in white
void drawColumns(const Bitmap1 &b, int x, int y, int c0, int c1) {
  int rowBytes = (b.w + 7) / 8;

  for (int xx = c0; xx < c1; xx++) {
    for (int yy = 0; yy < b.h; yy++) {
      if (b.px[yy * rowBytes + xx / 8] & (0x80 >> (xx % 8))) {
        tft.drawPixel(x + xx, y + yy, TFT_WHITE);
      }
    }
  }
}

// Returns once Wi-Fi has been up for SPLASH_HOLD_MS (right after the drawing
//  with no network to join), after SPLASH_MAX_MS, or on a tap.
// It waits for a finger to lift first, since a press may turn into a hold.
void splash() {
  unsigned long start = millis();
  clearScreen();
  bool skipped = false;

  float total = 0, drawn = 0;
  walkRex([&](float, float, float, float, float seg) { total += seg; });

  while (!skipped && drawn < total) {
    // Slow at first, like a pen finding its line
    float t = min(1.0f, (millis() - start) / (float)SPLASH_TRACE_MS);
    float to = total * powf(t, 1.6f);
    traceRex(drawn, to);
    drawn = to;
    splashStatus(start);
    skipped = splashInput();
    delay(5);
  }

  struct { const Bitmap1 &b; int x, y; } words[] = {{WORD_CHEAP, 172, 66}, {WORD_REX, 176, 106}, {WORD_DISPLAY, 172, 148}};

  for (auto &w : words) {
    unsigned long wordStart = millis();
    int done = 0;
    while (!skipped && done < w.b.w) {
      int upto = min(w.b.w, (int)((millis() - wordStart) * w.b.w / SPLASH_WORD_MS) + 1);
      drawColumns(w.b, w.x, w.y, done, upto);
      done = upto;
      splashStatus(start);
      skipped = splashInput();
      delay(10);
    }
  }

  unsigned long upSince = 0;
  while (!skipped) {
    if ((WiFi.status() == WL_CONNECTED || wifiFrom == WIFI_NONE) && upSince == 0) {
      upSince = millis();
    }

    bool done = (upSince != 0 && millis() - upSince >= SPLASH_HOLD_MS) || millis() - start >= SPLASH_MAX_MS;
    if (done && splashPressStart == 0) {
      break;
    }

    splashStatus(start);
    skipped = splashInput();
    delay(10);
  }

  // Let go first, so the skipping tap doesn't land on a row.
  while (touch.touched()) {
    delay(10);
  }
  clearScreen();
}

void setup() {
  Serial.begin(115200);
  improvBegin(improvDevice);

  ledcAttach(LED_RED, 5000, 8);
  ledcAttach(LED_GREEN, 5000, 8);
  ledcAttach(LED_BLUE, 5000, 8);
  setLed(0, 0, 0);

  tft.init();
  tft.setRotation(1);
  clearScreen();
  if (!canvas.createSprite(SCREEN_W, ROW_H_FINGER)) {
    Serial.println("No memory for the row canvas");
  }
  wobbleCanvas.setColorDepth(1);
  if (!wobbleCanvas.createSprite(WOBBLE_W, WOBBLE_H)) {
    Serial.println("No memory for the calm screen's Rex");
  }
  wobbleCanvas.setBitmapColor(TFT_WHITE, TFT_BLACK);
  drawWobbleRex(0);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);
  loadRowStyle();

  // The password lives in the settings only, not in a second copy the Wi-Fi
  //  driver would keep in flash.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // The reason codes tell a weak signal (201: no AP found, 15: handshake timeout)
  //  from a wrong password (2, 15 on first join) on the serial port.
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("Wi-Fi lost, reason %d\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  chooseWifi();
  if (wifiFrom != WIFI_NONE) {
    beginWifi();
  } else {
    Serial.println("Wi-Fi: none saved and no secrets.h, setup after the splash");
  }

  splash();
  if (wifiFrom == WIFI_NONE) {
    runSetup("No Wi-Fi saved yet.");
  }

  server.on("/status", HTTP_POST, handleStatus);
  server.on("/heartbeat", HTTP_POST, handleHeartbeat);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/", HTTP_GET, handleDump);
  server.on("/screen.bmp", HTTP_GET, handleScreen);
  server.on("/api/settings", HTTP_GET, sendSettings);
  server.on("/api/settings", HTTP_POST, handleSettingsSave);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.begin();
  tapServer.begin();
  serving = true;
  Serial.println("cheap-rex-display started");
}

// A line a minute on the serial port, to tell a lost Wi-Fi link from a stuck
//  server when the board stops answering.
void logHealth() {
  static unsigned long lastLog = 0;
  if (millis() - lastLog < 60000) {
    return;
  }

  lastLog = millis();
  int taps = 0;
  for (auto &t : tapClients) {
    taps += t.client.connected();
  }

  Serial.printf("Health: up %lu s, Wi-Fi %d, RSSI %d dBm, heap %u, tap streams %d\n",
                millis() / 1000, WiFi.status(), WiFi.RSSI(), ESP.getFreeHeap(), taps);
}

void loop() {
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;

  // Long enough without Wi-Fi, and it's time for the setup hotspot.
  static unsigned long lastOnline = 0;
  if (connected) {
    lastOnline = millis();
  } else if (millis() - lastOnline > SETUP_AFTER_MS) {
    runSetup(String("Can't reach ") + wifiSsid + ", still trying.");
  }

  // Auto-reconnect can give up after a failed join and leave the link idle
  //  for good (seen with a weak signal), so start over every REJOIN_MS until it's up.
  static unsigned long lastJoin = 0;
  if (!connected && millis() - lastJoin > REJOIN_MS) {
    lastJoin = millis();
    // Whether the network is in range at all, and how strong: a blocking
    //  scan of about 2 s, only while offline.
    int found = WiFi.scanNetworks(), strongest = 0, channel = 0;
    for (int i = 0; i < found; i++) {
      if (WiFi.SSID(i) == wifiSsid && (channel == 0 || WiFi.RSSI(i) > strongest)) {
        strongest = WiFi.RSSI(i);
        channel = WiFi.channel(i);
      }
    }
    WiFi.scanDelete();
    if (channel) {
      Serial.printf("Wi-Fi status %d; network seen at %d dBm, channel %d, of %d networks; joining again\n",
                    WiFi.status(), strongest, channel, found);
    } else {
      Serial.printf("Wi-Fi status %d; network not seen among %d networks; joining again\n", WiFi.status(), found);
    }
    WiFi.disconnect();
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  }

  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      MDNS.begin(HOSTNAME);
      MDNS.addService("http", "tcp", 80);
      Serial.printf("Wi-Fi up: %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.println("Wi-Fi down");
    }
    dirty = true;
  }

  server.handleClient();
  improvPoll();
  if (screenTaken) {
    screenTaken = false;
    dirty = true;
  }
  acceptTapClient();
  pingTapClients();
  checkTouch();
  updateLed();
  logHealth();

  if (page > 0 && millis() - pageAt > PAGE_MS) {
    page = 0;
    dirty = true;
  }

  if (listUntil != 0 && (long)(millis() - listUntil) >= 0) {
    listUntil = 0;
    dirty = true;
  }

  // A source goes stale with time alone, without a request, so watch for it.
  static uint8_t staleMask = 0;
  uint8_t mask = 0;

  for (int i = 0; i < MAX_SOURCES; i++) {
    if (sources[i].used && isStale(sources[i].name)) {
      mask |= 1 << i;
    }
  }

  if (mask != staleMask) {
    staleMask = mask;
    dirty = true;
  }

  if ((long)(millis() - holdUntil) < 0) {
    return;
  }

  if (dirty && millis() - lastDraw >= REDRAW_MS) {
    dirty = false;
    lastDraw = millis();
    trackWaiting();
    drawHeader();
    drawRows();
  } else if (!dirty) {
    refreshAges();
    wobbleRex();
  }
}
