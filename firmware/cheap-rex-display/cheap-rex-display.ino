// cheap-rex-display firmware.
//
// Joins your Wi-Fi as crd.local and lists the agent status it is sent over
//  HTTP, most urgent first. The RGB LED shows the most urgent state and pulses
//  red while an agent waits for you. Display only: nothing here can answer or
//  approve anything in an agent session.
//
// HTTP API, form or query parameters, all plain text:
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
// A source that sends nothing for STALE_MS is shown grey and ignored by the
//  LED, so a sleeping laptop leaves no "working" behind.
//
// Taps go back to the watchers on port 81. A watcher keeps one request open,
//  GET /taps?source=NAME, and reads a line "tap KEY" for each tap on one of its
//  rows, and "ping" every PING_MS. A new request for a source replaces the old.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <TFT_eSPI.h>             // settings from config/TFT_eSPI/User_Setup.h, see the README
#include <XPT2046_Touchscreen.h>
#include "secrets.h"              // WIFI_SSID and WIFI_PASSWORD, from secrets.h.example
#include "rex_art.h"

#define HOSTNAME "crd"

// Rex at the left of the header: REX_PIXEL, REX_SOFT or REX_BOLD
#define HEADER_REX REX_PIXEL

// Boot splash: Rex traces himself while Wi-Fi connects, then the words are
//  written in. It waits for Wi-Fi up to SPLASH_MAX_MS, and a tap skips it.
#define SPLASH_REX_X 22
#define SPLASH_REX_Y 52
#define SPLASH_REX_H 136
#define SPLASH_TRACE_MS 3200
#define SPLASH_WORD_MS 360
#define SPLASH_HOLD_MS 1500
#define SPLASH_MAX_MS 10000
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

// What each drawn row shows, so a tap still names the right pane after the
//  entries change underneath it.
struct Row {
  char source[16];
  char key[80];
};

// Kept apart from sources: an open tap request must not make the board
//  answer "known" to a heartbeat when it holds nothing from that source.
struct TapClient {
  char source[16];
  NetworkClient client;
};

Entry entries[MAX_ENTRIES];
Source sources[MAX_SOURCES];
TapClient tapClients[MAX_SOURCES];
Row drawnRows[MAX_ENTRIES];
int drawnCount = 0;
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
WebServer server(80);
NetworkServer tapServer(TAP_PORT);
bool dirty = true;
unsigned long lastDraw = 0;
unsigned long holdUntil = 0;  // a tapped row's outline stays up until then

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

    out += String(" | ") + e.msg + " | " + e.key + "\n";
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
        tft.drawPixel(x + xx, y + yy, tft.color565(r + (255 - r) * a / 15, g + (255 - g) * a / 15, b + (255 - b) * a / 15));
      }
    }
  }
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_DARKGREY);
  // TFT_DARKGREY is 123, 125, 123
  drawBlended(HEADER_REX, 3, (HEADER_H - HEADER_REX.h) / 2, 123, 125, 123);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(ML_DATUM);
  String text = WiFi.status() == WL_CONNECTED ? String(HOSTNAME ".local ") + WiFi.localIP().toString() : String("Wi-Fi connecting...");
  tft.drawString(text, 3 + HEADER_REX.w + 5, HEADER_H / 2, 2);
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

void drawRows() {
  // Indices of used entries, most urgent first, one per pane.
  // A program can report several records (opencode sends one for the pane and one
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
  drawnCount = 0;
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
    strlcpy(drawnRows[r].source, e.source, sizeof(drawnRows[r].source));
    strlcpy(drawnRows[r].key, e.key, sizeof(drawnRows[r].key));
    drawnCount = r + 1;
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

// Watches the touch screen and sends a tap when a press on a row ends.
//  The press is placed at the average of its settled readings.
void checkTouch() {
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
    return;
  }

  if (pressStarted == 0 || now - lastSeen < TOUCH_RELEASE_MS) {
    return;
  }

  pressStarted = 0;
  if (readings == 0) {
    return;
  }

  int y = constrain(map(sumY / readings, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
  int r = (y - HEADER_H) / ROW_H;
  if (y < HEADER_H || r >= drawnCount) {
    return;
  }

  // Outline the row: white when a watcher took the tap, grey when none listens.
  bool sent = sendTap(drawnRows[r]);
  Serial.printf("Tap row %d %s %s: %s\n", r, drawnRows[r].source, drawnRows[r].key, sent ? "sent" : "no watcher");
  tft.drawRect(0, HEADER_H + r * ROW_H, SCREEN_W, ROW_H, sent ? TFT_WHITE : COLOR_GREY);
  holdUntil = now + TAP_FLASH_MS;
  dirty = true;
}

// A new press on the touch screen, for skipping the splash
bool tapped() {
  static bool down = false;
  bool now = touch.touched();
  bool edge = now && !down;
  down = now;
  return edge;
}

// The splash's bottom line: Wi-Fi progress, then the board's address.
void splashStatus(unsigned long start) {
  static String shown;
  String text = WiFi.status() == WL_CONNECTED
      ? String(HOSTNAME ".local ") + WiFi.localIP().toString()
      : String("Wi-Fi connecting") + String("...").substring(0, (millis() - start) / 333 % 3 + 1);
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

// Returns once Wi-Fi has been up for SPLASH_HOLD_MS, after
//  SPLASH_MAX_MS, or on a tap.
void splash() {
  unsigned long start = millis();
  tft.fillScreen(TFT_BLACK);
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
    skipped = tapped();
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
      skipped = tapped();
      delay(10);
    }
  }

  unsigned long upSince = 0;
  while (!skipped && millis() - start < SPLASH_MAX_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      if (upSince == 0) {
        upSince = millis();
      } else if (millis() - upSince >= SPLASH_HOLD_MS) {
        break;
      }
    }

    splashStatus(start);
    skipped = tapped();
    delay(10);
  }

  // Let go first, so the skipping tap doesn't land on a row.
  while (touch.touched()) {
    delay(10);
  }
  tft.fillScreen(TFT_BLACK);
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

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // The reason codes tell a weak signal (201: no AP found, 15: handshake timeout)
  //  from a wrong password (2, 15 on first join) on the serial port.
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("Wi-Fi lost, reason %d\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  splash();

  server.on("/status", HTTP_POST, handleStatus);
  server.on("/heartbeat", HTTP_POST, handleHeartbeat);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/", HTTP_GET, handleDump);
  server.on("/screen.bmp", HTTP_GET, handleScreen);
  server.begin();
  tapServer.begin();
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

  // Auto-reconnect can give up after a failed join and leave the link idle
  //  for good (seen with a weak signal), so start over every 30 s until it's up.
  static unsigned long lastJoin = 0;
  if (!connected && millis() - lastJoin > 30000) {
    lastJoin = millis();
    // Whether the network is in range at all, and how strong: a blocking
    //  scan of about 2 s, only while offline.
    int found = WiFi.scanNetworks(), strongest = 0, channel = 0;
    for (int i = 0; i < found; i++) {
      if (WiFi.SSID(i) == WIFI_SSID && (channel == 0 || WiFi.RSSI(i) > strongest)) {
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
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
  acceptTapClient();
  pingTapClients();
  checkTouch();
  updateLed();
  logHealth();

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

  if (dirty && millis() - lastDraw >= REDRAW_MS && (long)(millis() - holdUntil) >= 0) {
    dirty = false;
    lastDraw = millis();
    drawHeader();
    drawRows();
  }
}
