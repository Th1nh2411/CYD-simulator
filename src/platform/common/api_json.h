#pragma once

#include <cstddef>
#include <cstdint>

#include "app/engine/events.h"
#include "app/ui/characters.h"

// JSON shared by the two settings transports: the web server (Wi-Fi) and the
// serial protocol (USB). Both speak the same payloads.
namespace platform::api {

// Lists for the settings page: expressions, then per character its name,
// option lists and presets (CharacterSet::describe). Returns bytes written,
// 0 if out is too small.
size_t metaJson(const ui::CharacterSet& characters, char* out, size_t size);

// {"kind":"notification"|"prompt","title","body","options":[...],"ref","sticky","tag","face"}
// -> ShowCard event; sticky cards stay until tapped; tag and face are style
// hints (engine::CardContent). False on bad JSON or a prompt without options.
bool cardFromJson(const char* json, size_t len, engine::Event& out);

// {"type":"talk"|"chat"|...,"text",...}: a request for the device's brain (an AI
// on a PC, tools/capri), passed on as it is. False on bad JSON, no type, or over 600 bytes.
bool isAsk(const char* json, size_t len);

// {"epoch":<unix seconds>,"offset":<minutes east of UTC>} -> local time as
// seconds since the epoch. False on bad JSON or a missing epoch.
bool localTimeFromJson(const char* json, size_t len, int64_t& localSeconds);

// {"rotation":0|90|180|270,"mirror":bool}, either key optional: fills the ones
// given (clockwise degrees; the caller checks the angle). False on bad JSON or
// a key of the wrong type.
bool screenFromJson(const char* json, size_t len, int& degrees, bool& mirror);

}  // namespace platform::api
