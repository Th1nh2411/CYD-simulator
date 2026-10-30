#include "app/net/aibuddy.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "app/core/utf8.h"

namespace aibuddy {

namespace {

constexpr uint32_t kResendMs = 3000;          // as the reference fake device (simulator-real.js)
constexpr int64_t kClockValidAfter = 1700000000;
// What the device reports it can take (deviceInfo): the engine's card queue holds 6
constexpr int kListCapacity = 6, kMaxFrameBytes = 2048;

const char* const kStateNames[] = {"off", "connecting", "need-otp", "logging-in", "logged-in", "refused"};

struct Reason {
  const char* code;
  const char* text;
};
// Login failure codes of AI Buddy's MQ server (contracts LOGIN_FAILURE_CODES)
const Reason kReasons[] = {
    {"otp_invalid", "OTP가 맞지 않아요. 다시 넣어 주세요."},
    {"locked", "실패가 많아 잠겼어요. 잠시 뒤 다시."},
    {"wifi_not_allowed", "허용되지 않은 Wi-Fi예요."},
    {"device_not_in_tenant", "회사에 등록되지 않은 기기예요."},
    {"device_not_assigned", "아직 누구에게도 배정되지 않은 기기예요."},
    {"device_revoked", "회수된 기기예요."},
    {"tenant_suspended", "회사 이용이 정지됐어요."},
    {"session_expired", "세션이 만료됐어요. OTP를 다시 넣어 주세요."},
    {"session_ended", "세션이 끝났어요. OTP를 다시 넣어 주세요."},
    {"session_invalid", "세션을 모르는 기기예요. OTP를 다시 넣어 주세요."},
};

// Failure codes a new OTP can fix
bool otpFixes(const char* code) {
  for (const char* c : {"otp_invalid", "session_expired", "session_ended", "session_invalid"}) {
    if (strcmp(c, code) == 0) return true;
  }
  return false;
}

// "2026-10-30T09:00:00.000Z" as Unix seconds, 0 if it does not read
int64_t parseIso(const char* s) {
  int y, mo, d, h, mi, sec;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
  // Days from the civil date (Howard Hinnant's algorithm): no timegm everywhere
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = unsigned((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = int64_t(era) * 146097 + int64_t(doe) - 719468;
  return days * 86400 + h * 3600 + mi * 60 + sec;
}

void formatUtc(int64_t t, const char* format, char* out, size_t size) {
  const time_t tt = time_t(t);
  struct tm parts;
  gmtime_r(&tt, &parts);
  strftime(out, size, format, &parts);
}

}  // namespace

const char* reasonText(const char* code) {
  if (!code || !*code) return "";
  for (const Reason& r : kReasons) {
    if (strcmp(r.code, code) == 0) return r.text;
  }
  return "";
}

const char* Client::stateName() const { return kStateNames[int(state_)]; }

bool Client::korean() const { return strcmp(lang_, "ko") == 0; }

void Client::setDevice(const char* deviceNo, int tzMinutes, const char* session) {
  tzMin_ = tzMinutes;
  if (strcmp(deviceNo, device_) == 0 && state_ != State::Off) return;
  snprintf(device_, sizeof(device_), "%s", deviceNo);
  snprintf(session_, sizeof(session_), "%s", session);
  state_ = *device_ ? State::Connecting : State::Off;
  pendingCount_ = 0;
  for (Ref& r : refs_) r = Ref();
}

void Client::session(const char* token) {
  snprintf(session_, sizeof(session_), "%s", token);
  port_.saveSession(device_, token);
}

void Client::isoNow(char* out, size_t size) { formatUtc(port_.utcNow(), "%Y-%m-%dT%H:%M:%SZ", out, size); }

// A time the engine sent, as the user reads it: "18:00" today, else "11/02 18:00"
void Client::localTime(const char* iso, char* out, size_t size) {
  const int64_t at = parseIso(iso) + int64_t(tzMin_) * 60, now = port_.utcNow() + int64_t(tzMin_) * 60;
  const bool today = now > kClockValidAfter && at / 86400 == now / 86400;
  formatUtc(at, today ? "%H:%M" : "%m/%d %H:%M", out, size);
}

void Client::uuid(char out[37]) {
  uint8_t b[16];
  port_.randomBytes(b, sizeof(b));
  b[6] = (b[6] & 0x0F) | 0x40;  // version 4
  b[8] = (b[8] & 0x3F) | 0x80;  // RFC 4122 variant
  snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
           b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

void Client::login(const char* otp) {
  JsonDocument doc;
  doc["t"] = "login";
  doc["method"] = otp ? "otp" : "session";
  JsonObject info = doc["deviceInfo"].to<JsonObject>();
  info["firmwareVersion"] = kFirmwareVersion;
  info["maxFrameBytes"] = kMaxFrameBytes;
  info["listCapacity"] = kListCapacity;
  JsonArray packs = info["languagePacks"].to<JsonArray>();
  packs.add("en");
  packs.add("ko");
  if (otp) doc["otp"] = otp;
  else doc["sessionToken"] = session_;
  char hash[65];
  if (port_.wifiHash(hash)) doc["wifiHash"] = hash;
  else doc["wifiHash"] = nullptr;
  if (port_.utcNow() > kClockValidAfter) {
    char at[24];
    isoNow(at, sizeof(at));
    doc["at"] = at;
  }
  bySession_ = !otp;
  state_ = State::LoggingIn;
  char json[512];
  const size_t n = serializeJson(doc, json, sizeof(json));
  char line[64];
  snprintf(line, sizeof(line), "aibuddy: %s login as %s", otp ? "OTP" : "session", device_);
  port_.log(line);
  if (n < sizeof(json)) port_.publishUp(json, n);
}

void Client::askOtp(const char* reason) {
  state_ = State::NeedOtp;
  port_.needOtp(device_, reason);
}

void Client::note(const char* tag, const char* title, const char* body) {
  engine::Event e;
  e.type = engine::EventType::ShowCard;
  e.card.kind = engine::CardKind::Notification;
  core::copyUtf8(e.card.tag, sizeof(e.card.tag), tag);
  core::copyUtf8(e.card.title, sizeof(e.card.title), title);
  core::copyUtf8(e.card.body, sizeof(e.card.body), body);
  port_.postCard(e);
}

void Client::refuse(const char* code) {
  state_ = State::Refused;
  const char* text = reasonText(code);
  note("error", "AI Buddy", *text ? text : code);
}

void Client::onConnected() {
  if (!*device_) return;
  if (session_[0]) login(nullptr);
  else askOtp("");
}

void Client::onDisconnected() {
  if (state_ != State::Off) state_ = State::Connecting;
}

bool Client::submitOtp(const char* code) {
  if (strlen(code) != 6 || strspn(code, "0123456789") != 6) return false;
  if (state_ != State::NeedOtp && state_ != State::Refused) return false;
  login(code);
  return true;
}

void Client::onAuth(JsonObjectConst f) {
  if (f["isOk"] | false) {
    state_ = State::LoggedIn;
    core::copyUtf8(name_, sizeof(name_), f["displayName"] | "");
    core::copyUtf8(lang_, sizeof(lang_), f["language"] | "en");
    if (f["sessionToken"].is<const char*>()) session(f["sessionToken"]);
    char body[96];
    snprintf(body, sizeof(body), "%s · %s", name_, device_);
    note("start", "AI Buddy", body);
    for (int i = 0; i < pendingCount_; i++) port_.publishUp(pending_[i].json, strlen(pending_[i].json));  // E15-1
    return;
  }
  const char* code = f["code"] | "";
  if (bySession_) session("");
  if (bySession_ || otpFixes(code)) askOtp(code);
  else refuse(code);
}

void Client::onNoti(JsonObjectConst f) {
  const char* kind = f["kind"] | "notice";
  const char* priority = f["priority"] | "normal";
  const bool task = strcmp(kind, "task") == 0, schedule = strcmp(kind, "schedule") == 0, ko = korean();
  Ref& r = refs_[nextSlot_];
  nextSlot_ = (nextSlot_ + 1) % kRefs;  // a card older than eight others is long answered or gone
  r = Ref();
  r.ref = nextRef_++;
  core::copyUtf8(r.key, sizeof(r.key), f["notificationKey"] | "");
  core::copyUtf8(r.messageId, sizeof(r.messageId), f["messageId"] | "");

  engine::Event e;
  e.type = engine::EventType::ShowCard;
  engine::CardContent& c = e.card;
  c.kind = engine::CardKind::Prompt;
  c.sticky = true;  // on the device until answered, as on the engine's list
  c.ref = r.ref;
  const bool loud = strcmp(priority, "urgent") == 0 || strcmp(priority, "high") == 0;
  const bool rejected = f["isRejected"] | false;
  core::copyUtf8(c.tag, sizeof(c.tag), rejected ? "error" : loud ? "alert" : task ? "work" : schedule ? "remind" : "info");
  const char* title = task ? (ko ? "할 일" : "Task") : schedule ? (ko ? "일정" : "Schedule") : (ko ? "공지" : "Notice");
  snprintf(c.title, sizeof(c.title), "%s%s", strcmp(priority, "urgent") == 0 ? "! " : "", title);
  char when[32] = "", body[200], t[16];
  if (f["dueAt"].is<const char*>()) {
    localTime(f["dueAt"], t, sizeof(t));
    snprintf(when, sizeof(when), ko ? " (마감 %s)" : " (due %s)", t);
  } else if (f["startsAt"].is<const char*>()) {
    localTime(f["startsAt"], t, sizeof(t));
    snprintf(when, sizeof(when), ko ? " (%s 시작)" : " (starts %s)", t);
  }
  snprintf(body, sizeof(body), "%s%s", f["body"] | "", when);
  core::copyUtf8(c.body, sizeof(c.body), body);
  static const char* const kTask[] = {"done", "later", "seen"};
  static const char* const kOther[] = {"seen", "later"};
  const char* const* responses = task ? kTask : kOther;
  c.optionCount = task ? 3 : 2;
  for (int i = 0; i < c.optionCount; i++) {
    r.responses[i] = responses[i];
    const char* label = strcmp(responses[i], "done") == 0    ? (ko ? "완료" : "Done")
                        : strcmp(responses[i], "later") == 0 ? (ko ? "나중에" : "Later")
                                                             : (ko ? "확인" : "Seen");
    core::copyUtf8(c.options[i], sizeof(c.options[i]), label);
  }
  char line[200];
  snprintf(line, sizeof(line), "aibuddy: noti %s (%s %s) as card %u", r.key, kind, priority, unsigned(r.ref));
  port_.log(line);
  port_.postCard(e);
}

void Client::onSummary(JsonObjectConst f) {
  const bool ko = korean();
  char body[200];
  const int n = snprintf(body, sizeof(body), ko ? "열린 할 일 %d · 오늘 마감 %d" : "Open tasks %d · due today %d",
                         int(f["openTaskCount"] | 0), int(f["dueTodayCount"] | 0));
  JsonObjectConst next = f["nextSchedule"].as<JsonObjectConst>();
  if (!next.isNull() && n > 0 && size_t(n) < sizeof(body)) {
    char t[16];
    localTime(next["startsAt"], t, sizeof(t));
    snprintf(body + n, sizeof(body) - size_t(n), ko ? " · 다음: %s %s" : " · next: %s %s", next["title"] | "", t);
  }
  note("info", ko ? "오늘" : "Today", body);
}

void Client::onFrame(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) {
    port_.log("aibuddy: frame is not JSON");
    return;
  }
  JsonObjectConst f = doc.as<JsonObjectConst>();
  const char* t = f["t"] | "";
  if (strcmp(t, "auth") == 0) {
    onAuth(f);
  } else if (strcmp(t, "logout") == 0) {
    const char* reason = f["reason"] | "";
    session("");
    if (strcmp(reason, "session_ended") == 0) askOtp(reason);
    else refuse(reason);
  } else if (strcmp(t, "reauth") == 0) {
    onConnected();
  } else if (strcmp(t, "settings") == 0) {
    core::copyUtf8(name_, sizeof(name_), f["displayName"] | name_);
    core::copyUtf8(lang_, sizeof(lang_), f["language"] | lang_);
  } else if (strcmp(t, "ackok") == 0) {
    const char* id = f["messageId"] | "";
    for (int i = 0; i < pendingCount_; i++) {
      if (strcmp(pending_[i].messageId, id) != 0) continue;
      pending_[i] = pending_[--pendingCount_];
      break;
    }
  } else if (strcmp(t, "noti") == 0) {
    onNoti(f);
  } else if (strcmp(t, "cancel") == 0) {
    // The card may be up already: answering it then sends nothing
    for (Ref& r : refs_) {
      if (r.ref && strcmp(r.key, f["notificationKey"] | "") == 0) r.cancelled = true;
    }
  } else if (strcmp(t, "summary") == 0) {
    onSummary(f);
  } else {
    port_.log("aibuddy: unknown frame");
  }
}

bool Client::onOutcome(const engine::Outcome& o) {
  for (Ref& r : refs_) {
    if (!r.ref || r.ref != o.ref) continue;
    const bool send = !r.cancelled && o.option >= 0 && o.option < engine::kMaxOptions && r.responses[o.option];
    if (send && pendingCount_ < kPending) {
      JsonDocument doc;
      Pending& p = pending_[pendingCount_];
      uuid(p.messageId);
      char at[24];
      isoNow(at, sizeof(at));
      doc["t"] = "ack";
      doc["messageId"] = p.messageId;
      doc["notificationKey"] = r.key;
      doc["response"] = r.responses[o.option];
      doc["at"] = at;
      if (r.messageId[0]) doc["sourceMessageId"] = r.messageId;
      const size_t n = serializeJson(doc, p.json, sizeof(p.json));
      if (n < sizeof(p.json)) {
        pendingCount_++;
        if (state_ == State::LoggedIn) port_.publishUp(p.json, n);
      }
    }
    r = Ref();
    return true;
  }
  return false;
}

void Client::poll(uint32_t now) {
  if (state_ != State::LoggedIn || !pendingCount_ || now - resentMs_ < kResendMs) return;
  resentMs_ = now;
  for (int i = 0; i < pendingCount_; i++) port_.publishUp(pending_[i].json, strlen(pending_[i].json));
}

}  // namespace aibuddy
