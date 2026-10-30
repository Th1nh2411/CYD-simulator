#pragma once

#include <cstddef>
#include <cstdint>

#include "app/engine/events.h"
#include "app/engine/world.h"

// AI Buddy's device protocol on the ESP32: the portable aibuddy::Client (app/net/aibuddy.h) behind
// a mutex, with NVS for the session token (the number it belongs to beside it), the station's SSID
// for the Wi-Fi hash and the hardware RNG. network.cpp carries the frames over MQTT: client id and
// user = number, password = secret key, d/<number>/dn in, /up out, /st status and last will.
namespace platform::aibuddy {

struct Hooks {
  bool (*postCard)(const engine::Event& e) = nullptr;  // MQTT task
  // The login needs the OTP: reason = the failure code that sent it back, "" the first time. MQTT task
  void (*needOtp)(const char* deviceNo, const char* reason) = nullptr;
  // Publishes a frame on d/<number>/up at QoS 1; false while MQTT is down. Any thread
  bool (*publishUp)(const char* json, size_t len) = nullptr;
};

void begin(const Hooks& hooks);
// The number the MQTT client connects as ("" = protocol off) and the local time zone for the
// times it shows. A new number forgets the saved session. Loop task, while no client runs.
void setDevice(const char* deviceNo, int tzMinutes);
const char* device();

void onConnected();                          // MQTT task: subscribed and status sent, logs in
void onFrame(const char* json, size_t len);  // MQTT task: a frame from d/<number>/dn
void onDisconnected();                       // MQTT task
// Loop task: a prompt answered. True when it was one of ours (sent as an ack, or dropped when
// cancelled meanwhile); false leaves it to whoever sent the card.
bool onOutcome(const engine::Outcome& o);
bool submitOtp(const char* code);  // any thread: 6 digits; false when no login waits for one
void poll(uint32_t nowMs);         // loop task: resends unconfirmed answers

// off | connecting | need-otp | logging-in | logged-in | refused, and the user's name once logged in
const char* state();
const char* displayName();

}  // namespace platform::aibuddy
