#pragma once

#include <cstddef>
#include <cstdint>

#include "app/engine/events.h"
#include "app/engine/world.h"

// Wi-Fi and the MQTT link. The station joins the home Wi-Fi when one is saved,
// the device's own AP serves the settings page when asked (see apActive()), and
// an MQTT client (esp-mqtt, its own task) talks to a broker over either of them.
// Settings are one JSON string in NVS (key "net"), saved as soon as they change.
// poll() on the loop task applies them: it retries the home Wi-Fi with backoff
// (each attempt scans and briefly upsets the AP) and restarts the MQTT client
// when its settings change. No task of its own: its stack is RAM the TLS needs.
//
// MQTT topics under <topic> (default "blob/<last 4 hex digits of the MAC>"):
//   <topic>/card     in   same JSON as POST /api/card
//   <topic>/time     in   same JSON as POST /api/time
//   <topic>/outcome  out  {"ref":N,"option":i} for every prompt; -1 = expired unanswered
//   <topic>/battery  out  retained {"mv":4090} every 5 minutes, boards with a battery pin only
//   <topic>/battery/get  in  any payload: answers on <topic>/battery at once
//   <topic>/ask      out  requests for the device's brain (an AI on a PC, tools/capri):
//                         {"type":"talk"|"chat","text"?,"lang"}; it answers with cards
//   <topic>/status   out  retained {"online":true,"ip",...}; the broker publishes
//                         {"online":false} when the device drops off (last will)
// Inputs the broker replays from its retained store are skipped: they would come
// back with every reconnect.
//
// AI Buddy mode ("aibuddy":{"device","secret"} set): the client connects as that device to AI
// Buddy's MQ server instead (aibuddy.h): d/<number>/dn in, /up out, /st status and last will;
// none of the topics above, and outcomes, battery, debug and asks are not sent.
namespace platform::net {

struct Hooks {
  bool (*postCard)(const engine::Event& e) = nullptr;   // called from the MQTT task
  void (*setTime)(int64_t localSeconds) = nullptr;       // called from the MQTT task
  void (*batteryRequest)() = nullptr;                    // called from the MQTT task
  // AI Buddy wants the login OTP (aibuddy.h); reason: why it is asked again. MQTT task
  void (*needOtp)(const char* deviceNo, const char* reason) = nullptr;
};

// Loads the saved settings and brings up the station, or the AP when no home
// Wi-Fi is saved; poll() starts the rest. name
// doubles as the AP name; its lowercase form is the host name the home router
// sees and the MQTT client id. No mDNS: it cost 7.8 KB of heap and 25 KB of
// flash, both short on the CYD. Call once from setup().
void begin(const char* name, const char* apPassword, const char* boardName, const Hooks& hooks);

// Loop task, as often as it likes. Returns at once except when the broker
// settings changed: then it waits for the old client to stop.
void poll(uint32_t nowMs);

// The device's own AP, the settings page's way in without the home network. It is
// up at boot only while no home Wi-Fi is saved; poll() brings it up when the home
// Wi-Fi cannot be joined for a minute, and setAp(true) does (the settings menu).
// It costs RAM, so main drops it again once the settings page has been idle.
// Loop task. apRequired: no home Wi-Fi saved, setAp(false) does nothing then.
bool apActive();
bool apRequired();
void setAp(bool on);

// Settings and live state; passwords are never sent back, only whether one is set:
// {"wifi":{"ssid","passSet","status","error","ip","rssi"},
//  "mqtt":{"host","port","tls","user","passSet","ownCa","clientCert","clientKey","topic","status","error"},
//  "time":{"server","tz","synced"},"aibuddy":{"device","secretSet","state","user"}}
// time: the SNTP server asked first (default time.google.com, then pool.ntp.org and
// time.cloudflare.com), tz = minutes east of UTC (default 540, Korea; no daylight saving),
// synced = the wall clock has been set from the network.
// status: off | connecting | connected. Returns bytes written, 0 if out is too small.
size_t toJson(char* out, size_t size);

// Partial {"wifi":{"ssid","pass"},"mqtt":{"host","port","tls","user","pass","topic"},
//          "time":{"server","tz"},"aibuddy":{"device","secret"}}:
// keys left out keep their value, "topic":"" restores the default. Saved and
// applied right away (reconnects). False, changing nothing, on bad JSON or a
// value that does not fit.
bool merge(const char* json, size_t len);

// Any thread: handed to the MQTT task, which sends it once connected
void publishOutcome(const engine::Outcome& o);

// Mutual TLS, test stage (the private key sits in NVS until the digital signature peripheral takes
// over). With TLS on: the CA the broker's certificate must come from instead of the public ones, and
// the device's certificate with its private key (both needed), each as PEM. Saved at once, the MQTT
// client restarts. An empty PEM clears. False when it is not PEM or does not fit (3000 bytes).
// Never read back, only whether it is there (/api/net: ownCa, clientCert, clientKey).
enum class Pem : uint8_t { Ca, ClientCert, ClientKey };
bool setPem(Pem which, const char* pem, size_t len);

// Any thread: the PEM kept by setPem() as a malloc'd string for the caller to free, or nullptr
// when there is none. For other TLS clients of the device (the over-the-air update).
char* copyPem(Pem which);

// Any thread: stops the MQTT client (and starts it again), for the RAM of its TLS buffers while
// an update downloads. poll() does it; mqttDown() says when it has.
void setMqttPaused(bool paused);
bool mqttDown();

// Any thread: {"mv":4090,"pct":87} to <topic>/battery, retained. False, sending nothing, when MQTT
// is not up.
bool publishBattery(int millivolts, int percent);

// Any thread: a request to <topic>/ask (JSON, see above). False, sending nothing, when MQTT is not up.
bool publishAsk(const char* json, size_t len);

// Any thread: a line of text to <topic>/debug (diagnostics when no serial port is attached).
// False, sending nothing, when MQTT is not up.
bool publishDebug(const char* text);

}  // namespace platform::net
