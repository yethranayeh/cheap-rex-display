#include "improv_serial.h"
#include <WiFi.h>

namespace {

const uint8_t VERSION = 1;

// Packet types
const uint8_t TYPE_STATE = 0x01;
const uint8_t TYPE_ERROR = 0x02;
const uint8_t TYPE_RPC = 0x03;
const uint8_t TYPE_RESULT = 0x04;

// States. Serial needs no authorization, so the board starts out ready.
const uint8_t STATE_READY = 0x02;
const uint8_t STATE_PROVISIONING = 0x03;
const uint8_t STATE_PROVISIONED = 0x04;

// Errors
const uint8_t ERROR_NONE = 0x00;
const uint8_t ERROR_INVALID_RPC = 0x01;
const uint8_t ERROR_UNKNOWN_RPC = 0x02;
const uint8_t ERROR_UNABLE_TO_CONNECT = 0x03;

// RPC commands. The others (hostname, device name, network state) are optional,
//  and the board answers them with ERROR_UNKNOWN_RPC.
const uint8_t RPC_WIFI_SETTINGS = 0x01;
const uint8_t RPC_CURRENT_STATE = 0x02;
const uint8_t RPC_DEVICE_INFO = 0x03;
const uint8_t RPC_SCAN = 0x04;

// A packet that stops halfway is dropped after this long.
const unsigned long BYTE_GAP_MS = 500;

const ImprovDevice *device = nullptr;
uint8_t packet[9 + 255 + 1];  // header, version, type, length, data, checksum
size_t received = 0;
unsigned long lastByte = 0;

void sendPacket(uint8_t type, const uint8_t *data, size_t length) {
  uint8_t out[9 + 255 + 2];
  memcpy(out, "IMPROV", 6);
  out[6] = VERSION;
  out[7] = type;
  out[8] = length;
  memcpy(out + 9, data, length);

  uint8_t sum = 0;
  for (size_t i = 0; i < 9 + length; i++) {
    sum += out[i];
  }

  out[9 + length] = sum;
  // The installer reads packets at the start of a line, so the newline keeps
  //  the log lines that follow apart from it.
  out[10 + length] = '\n';
  Serial.write(out, 11 + length);
}

void sendState(uint8_t state) {
  sendPacket(TYPE_STATE, &state, 1);
}

void sendError(uint8_t error) {
  sendPacket(TYPE_ERROR, &error, 1);
}

// An RPC result for `command`, carrying `count` strings. No strings at all marks
//  the end of a scan.
void sendResult(uint8_t command, const String *strings, int count) {
  uint8_t data[255];
  size_t n = 2;
  data[0] = command;

  for (int i = 0; i < count; i++) {
    size_t length = strings[i].length();
    if (n + 1 + length > sizeof(data)) {
      break;
    }

    data[n++] = length;
    memcpy(data + n, strings[i].c_str(), length);
    n += length;
  }

  data[1] = n - 2;
  sendPacket(TYPE_RESULT, data, n);
}

void answerState() {
  if (!device->online()) {
    sendState(STATE_READY);
    return;
  }

  sendState(STATE_PROVISIONED);
  String url = device->url();
  sendResult(RPC_CURRENT_STATE, &url, 1);
}

void answerInfo() {
  String info[] = {device->firmware, device->version, device->chip, device->name};
  sendResult(RPC_DEVICE_INFO, info, 4);
}

// One result per network name, and an empty one at the end. Hidden networks
//  have no name to pick, so they are left out.
void answerScan() {
  int found = WiFi.scanNetworks();
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    bool repeated = false;
    for (int j = 0; j < i && !repeated; j++) {
      repeated = WiFi.SSID(j) == ssid;
    }

    if (ssid.isEmpty() || repeated) {
      continue;
    }

    String network[] = {ssid, String(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
    sendResult(RPC_SCAN, network, 3);
  }

  WiFi.scanDelete();
  sendResult(RPC_SCAN, nullptr, 0);
}

// data: SSID length, SSID, password length, password
void provision(const uint8_t *data, size_t length) {
  size_t ssidLength = length > 0 ? data[0] : 0;
  if (ssidLength == 0 || 1 + ssidLength >= length || 2 + ssidLength + data[1 + ssidLength] > length) {
    sendError(ERROR_INVALID_RPC);
    return;
  }

  String ssid, password;
  ssid.concat((const char *)data + 1, ssidLength);
  password.concat((const char *)data + 2 + ssidLength, data[1 + ssidLength]);

  sendError(ERROR_NONE);
  sendState(STATE_PROVISIONING);
  if (!device->join(ssid, password)) {
    sendError(ERROR_UNABLE_TO_CONNECT);
    sendState(STATE_READY);
    return;
  }

  sendState(STATE_PROVISIONED);
  String url = device->url();
  sendResult(RPC_WIFI_SETTINGS, &url, 1);
  Serial.flush();
  device->joined();
}

// A whole packet is in `packet`
void handlePacket() {
  size_t length = packet[8];
  uint8_t sum = 0;
  for (size_t i = 0; i < 9 + length; i++) {
    sum += packet[i];
  }

  if (sum != packet[9 + length]) {
    sendError(ERROR_INVALID_RPC);
    return;
  }

  // The installer only sends RPC commands: command, data length, data.
  const uint8_t *rpc = packet + 9;
  if (packet[6] != VERSION || packet[7] != TYPE_RPC || length < 2 || rpc[1] != length - 2) {
    sendError(ERROR_INVALID_RPC);
    return;
  }

  switch (rpc[0]) {
    case RPC_WIFI_SETTINGS: provision(rpc + 2, rpc[1]); break;
    case RPC_CURRENT_STATE: answerState(); break;
    case RPC_DEVICE_INFO:   answerInfo(); break;
    case RPC_SCAN:          answerScan(); break;
    default:                sendError(ERROR_UNKNOWN_RPC);
  }
}

}  // namespace

void improvBegin(const ImprovDevice &d) {
  device = &d;
}

void improvPoll() {
  while (device && Serial.available()) {
    uint8_t b = Serial.read();
    if (received > 0 && millis() - lastByte > BYTE_GAP_MS) {
      received = 0;
    }
    lastByte = millis();

    // Anything before "IMPROV" is skipped, such as a serial monitor's typing.
    if (received < 6 && b != "IMPROV"[received]) {
      received = 0;
      if (b != 'I') {
        continue;
      }
    }

    packet[received++] = b;
    if (received > 8 && received == 10 + (size_t)packet[8]) {
      received = 0;
      handlePacket();
    }
  }
}
