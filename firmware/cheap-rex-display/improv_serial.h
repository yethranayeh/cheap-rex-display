// Improv Wi-Fi over serial: how ESP Web Tools hands Wi-Fi to the board right
//  after flashing it, over the same USB cable at 115200 baud.
//  Spec: https://www.improv-wifi.com/serial/
//
// Packets in both directions:
//   "IMPROV", version 1, type, data length, data, checksum (low byte of the sum of
//   everything before it), and a newline.
// The installer sends RPC commands. The board answers with its state
//  (ready or provisioned), an error, or an RPC result: the command it answers,
//  the data length, then each string with its length in front.
//
// This file only speaks the protocol. Joining, saving and restarting stay with
//  the sketch, through ImprovDevice.

#pragma once
#include <Arduino.h>

struct ImprovDevice {
  // Answered to "device info". ESP Web Tools compares firmware and version with
  //  its manifest to offer an update.
  const char *firmware, *version, *chip, *name;
  // True while the board is on Wi-Fi: reported as provisioned, with url().
  bool (*online)();
  // Where the board answers once online, for the installer's "Visit device".
  String (*url)();
  // Joins the network and saves it when that works. Blocks until it is joined
  //  or has given up, which must be well within the installer's 45 s.
  bool (*join)(const String &ssid, const String &password);
  // Runs once the installer has been told it worked, for example a restart.
  void (*joined)();
};

// Keeps a pointer to `device`, so it must outlive the sketch: a global.
void improvBegin(const ImprovDevice &device);

// Reads what has arrived on Serial and answers it. Call it often: the installer
//  gives up when the board doesn't answer within 1.5 s (10 s right after flashing).
//  Nothing else in the sketch may read Serial.
void improvPoll();
