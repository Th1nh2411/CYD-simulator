#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "app/engine/events.h"
#include "app/engine/world.h"

// AI Buddy's device protocol (ai_services: apps/mq-server, docs/design/mq-server/device-connection.md),
// test phase JSON frames, without the transport: the platform carries the frames (MQTT on the
// device, the simulator's bridge in the browser) and supplies the rest through Port.
//
// The device has a number (HB-0001) and a secret key; it connects as that number to
// d/<number>/dn (in), /up (out), /st (status). Once connected it logs in with the session token it
// was given, or with the 6-digit OTP from the backoffice. Notifications become sticky prompt cards
// (Done / Later / Seen); an answer goes back as an ack, resent until the engine confirms it.
// One thread at a time (the platform serializes the calls).
namespace aibuddy {

class Port {
 public:
  virtual ~Port() = default;
  virtual bool publishUp(const char* json, size_t len) = 0;  // false while the link is down
  virtual void postCard(const engine::Event& e) = 0;
  // The login needs the OTP: reason = the failure code that sent it back, "" the first time
  virtual void needOtp(const char* deviceNo, const char* reason) = 0;
  virtual void saveSession(const char* deviceNo, const char* token) = 0;  // "" forgets it
  virtual int64_t utcNow() = 0;                // Unix seconds, <= 0 while unknown
  virtual bool wifiHash(char out[65]) = 0;     // SHA-256 hex of the current Wi-Fi name
  virtual void randomBytes(uint8_t* out, size_t n) = 0;
  virtual void log(const char* /*line*/) {}
};

// The version AI Buddy sees in deviceInfo: semantic, as its contract checks (MqDeviceInfo)
constexpr const char* kFirmwareVersion = "0.9.0";

class Client {
 public:
  enum class State : uint8_t { Off, Connecting, NeedOtp, LoggingIn, LoggedIn, Refused };

  explicit Client(Port& port) : port_(port) {}
  // The number in use ("" = off), the local time zone for the times it shows, and the session
  // token saved for that number ("" = none)
  void setDevice(const char* deviceNo, int tzMinutes, const char* session);
  const char* device() const { return device_; }

  void onConnected();  // the link is up: logs in
  void onDisconnected();
  void onFrame(const char* json, size_t len);  // a frame from d/<number>/dn
  // A prompt answered. True when the card was one of ours (sent as an ack, or dropped when it was
  // cancelled meanwhile); false leaves it to whoever sent the card.
  bool onOutcome(const engine::Outcome& o);
  bool submitOtp(const char* code);  // 6 digits; false when no login waits for one
  void poll(uint32_t nowMs);         // resends unconfirmed answers

  State state() const { return state_; }
  const char* stateName() const;
  const char* displayName() const { return name_; }

 private:
  static constexpr int kRefs = 8, kPending = 8;
  // A notification on screen as a prompt card: its ref, what each option answers
  struct Ref {
    uint32_t ref = 0;
    char key[129] = "";
    char messageId[37] = "";
    const char* responses[engine::kMaxOptions] = {};
    bool cancelled = false;
  };
  struct Pending {
    char messageId[37] = "";
    char json[384] = "";
  };

  void login(const char* otp);
  void askOtp(const char* reason);
  void refuse(const char* code);
  void note(const char* tag, const char* title, const char* body);
  void session(const char* token);
  void isoNow(char* out, size_t size);
  void localTime(const char* iso, char* out, size_t size);
  void uuid(char out[37]);
  void onAuth(JsonObjectConst f);
  void onNoti(JsonObjectConst f);
  void onSummary(JsonObjectConst f);
  bool korean() const;

  Port& port_;
  char device_[16] = "";
  int tzMin_ = 540;
  char session_[192] = "";
  State state_ = State::Off;
  bool bySession_ = false;  // the login in flight uses the session token
  char name_[49] = "", lang_[8] = "en";
  Ref refs_[kRefs];
  int nextSlot_ = 0;
  uint32_t nextRef_ = 60000;
  Pending pending_[kPending];
  int pendingCount_ = 0;
  uint32_t resentMs_ = 0;
};

// Korean text for an AI Buddy login failure code ("" for none or an unknown one)
const char* reasonText(const char* code);

}  // namespace aibuddy
