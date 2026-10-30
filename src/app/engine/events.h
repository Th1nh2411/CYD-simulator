#pragma once

#include <cstdint>

#include "app/engine/world.h"

namespace engine {

// Semantic inputs. The UI translates raw touches/buttons into these, so the
// engine stays independent of screen layout and input hardware.
enum class EventType : uint8_t {
  TapCharacter,
  TapCard,    // tap on a notification card dismisses it
  TapOption,  // value = option index of the visible prompt
  Button,     // value = button id
  ShowCard,   // card = content to queue
};

struct Event {
  EventType type = EventType::Button;
  int32_t value = 0;
  CardContent card{};
};

}  // namespace engine
