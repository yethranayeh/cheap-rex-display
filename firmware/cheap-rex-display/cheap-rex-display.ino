// cheap-rex-display firmware.
//
// Joins your Wi-Fi as crd.local and lists the agent status it is sent over
//  HTTP, most urgent first. The RGB LED shows the most urgent state and pulses
//  red while an agent waits for you. Display only: nothing here can answer or
//  approve anything in an agent session.
//
// HTTP API, form or query parameters, all plain text:
//   POST /status  source, key, session, app, state, kind, msg
//                 source names the Rex server, key the terminal.
//                 state is idle, working, done, blocked, error or clear.
//                 clear removes key and every key below it (key#..., key/...).
//   POST /heartbeat  source. Replies "known", or "unknown" when this board has
//                 nothing from that source (after a restart), so the watcher
//                 sends everything again.
//   POST /reset   source. Drops everything from that source.
//   GET  /        plain-text dump of what the display holds.
// A source that sends nothing for STALE_MS is shown grey and ignored by the
//  LED, so a sleeping laptop leaves no "working" behind.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <TFT_eSPI.h>             // settings from config/TFT_eSPI/User_Setup.h, see the README
#include "secrets.h"              // WIFI_SSID and WIFI_PASSWORD, from secrets.h.example

#define HOSTNAME "crd"

// RGB LED, active low.
#define LED_RED 4
#define LED_GREEN 16
#define LED_BLUE 17

#define SCREEN_W 320
#define SCREEN_H 240
#define HEADER_H 24
#define ROW_H 24
#define MAX_ENTRIES 32
#define MAX_SOURCES 4
#define STALE_MS 60000
#define REDRAW_MS 200

#define COLOR_GREY 0x7BEF

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
};

struct Source {
  bool used;
  char name[16];
  unsigned long lastSeen;
};

Entry entries[MAX_ENTRIES];
Source sources[MAX_SOURCES];
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
bool dirty = true;
unsigned long lastDraw = 0;

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

    out += String(" | ") + e.msg + " | " + e.key + "\n";
  }
  server.send(200, "text/plain", out.isEmpty() ? String("empty\n") : out);
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  // Active low: a duty of 255 is off.
  ledcWrite(LED_RED, 255 - r);
  ledcWrite(LED_GREEN, 255 - g);
  ledcWrite(LED_BLUE, 255 - b);
}

void updateLed() {
  int best = STATE_COUNT;
  for (auto &e : entries) {
    if (e.used && !isStale(e.source) && e.state < best) {
      best = e.state;
    }
  }

  switch (best) {
    case BLOCKED: {
      // Pulse, so a waiting agent catches the eye.
      uint8_t level = (millis() / 4) % 512;
      setLed(level < 256 ? level : 511 - level, 0, 0);
      break;
    }
    case ERROR_:  setLed(120, 0, 120); break;
    case DONE:    setLed(0, 60, 0); break;
    case WORKING: setLed(0, 0, 25); break;
    default:      setLed(0, 0, 0);
  }
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(ML_DATUM);
  String text = WiFi.status() == WL_CONNECTED ? String(HOSTNAME ".local ") + WiFi.localIP().toString() : String("Wi-Fi connecting...");
  tft.drawString(text, 4, HEADER_H / 2, 2);
  // Sources at the right: white while live, black when stale.
  int x = SCREEN_W - 4;
  tft.setTextDatum(MR_DATUM);

  for (int i = MAX_SOURCES - 1; i >= 0; i--) {
    if (!sources[i].used) {
      continue;
    }

    bool stale = isStale(sources[i].name);
    tft.setTextColor(stale ? TFT_BLACK : TFT_WHITE, TFT_DARKGREY);
    tft.drawString(sources[i].name, x, HEADER_H / 2, 2);
    x -= tft.textWidth(sources[i].name, 2) + 8;
  }
}

// Length of the terminal part of a key, before the '#'
size_t terminalLength(const char *key) {
  const char *hash = strchr(key, '#');
  return hash ? hash - key : strlen(key);
}

bool sameTerminal(const Entry &a, const Entry &b) {
  size_t length = terminalLength(a.key);
  return strcmp(a.source, b.source) == 0
      && length == terminalLength(b.key)
      && strncmp(a.key, b.key, length) == 0;
}

bool listed(const int *order, int count, const Entry &e) {
  for (int i = 0; i < count; i++) {
    if (sameTerminal(entries[order[i]], e)) {
      return true;
    }
  }
  return false;
}

void drawRows() {
  // Indices of used entries, most urgent first, one per terminal. A program
  //  can report several records (opencode sends one for the terminal and one
  //  for its chat session), which would otherwise show as duplicate rows.
  int order[MAX_ENTRIES], count = 0;
  for (int s = 0; s < STATE_COUNT; s++) {
    for (int i = 0; i < MAX_ENTRIES; i++) {
      if (entries[i].used && entries[i].state == s && !listed(order, count, entries[i])) {
        order[count++] = i;
      }
    }
  }

  int rows = (SCREEN_H - HEADER_H) / ROW_H;
  tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, TFT_BLACK);
  if (count == 0) {
    tft.setTextColor(COLOR_GREY, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No agent status yet", SCREEN_W / 2, (SCREEN_H + HEADER_H) / 2, 2);
    return;
  }
  for (int r = 0; r < rows && r < count; r++) {
    int y = HEADER_H + r * ROW_H;
    if (r == rows - 1 && count > rows) {
      tft.setTextColor(COLOR_GREY, TFT_BLACK);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(String("+") + (count - r) + " more", 4, y + ROW_H / 2, 2);
      break;
    }

    const Entry &e = entries[order[r]];
    bool stale = isStale(e.source);
    uint16_t chip = stale ? COLOR_GREY : stateColors[e.state];
    uint16_t text = stale ? COLOR_GREY : TFT_WHITE;

    tft.fillRect(0, y + 2, 6, ROW_H - 4, chip);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(chip, TFT_BLACK);
    String label = e.state == BLOCKED && e.kind[0] ? String(e.kind) : String(stateNames[e.state]);
    tft.drawString(label, 10, y + ROW_H / 2, 2);
    tft.setTextColor(text, TFT_BLACK);
    String who = String(e.session) + " " + e.app;
    tft.drawString(who, 86, y + ROW_H / 2, 2);

    // The message gets what is left of the row, cut to fit.
    String msg = e.msg;
    int msgX = 86 + tft.textWidth(who + "  ", 2);

    while (msg.length() > 0 && msgX + tft.textWidth(msg, 2) > SCREEN_W - 2) {
      msg.remove(msg.length() - 1);
    }

    tft.setTextColor(COLOR_GREY, TFT_BLACK);
    tft.drawString(msg, msgX, y + ROW_H / 2, 2);
  }
}

void setup() {
  Serial.begin(115200);

  ledcAttach(LED_RED, 5000, 8);
  ledcAttach(LED_GREEN, 5000, 8);
  ledcAttach(LED_BLUE, 5000, 8);
  setLed(0, 0, 0);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  server.on("/status", HTTP_POST, handleStatus);
  server.on("/heartbeat", HTTP_POST, handleHeartbeat);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/", HTTP_GET, handleDump);
  server.begin();
  Serial.println("cheap-rex-display started");
}

void loop() {
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;

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
  updateLed();

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

  if (dirty && millis() - lastDraw >= REDRAW_MS) {
    dirty = false;
    lastDraw = millis();
    drawHeader();
    drawRows();
  }
}
