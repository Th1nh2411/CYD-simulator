#pragma once

#include <cstdint>

// Engine data model. Plain data only: no pixels, no platform types.
// The UI turns this into graphics; the engine never knows how it is drawn.
namespace engine {

enum class Mood : uint8_t { Idle, Happy, Alert };

enum class CardKind : uint8_t { None, Notification, Prompt };

constexpr int kMaxOptions = 3;

// Something to show the user: a notification, or a prompt that needs an answer.
// Text is UTF-8; sizes are bytes, enough for 16 / 64 / 8 Hangul syllables.
struct CardContent {
  CardKind kind = CardKind::None;
  bool sticky = false;  // stays until tapped: no auto-dismiss, prompts never expire
  uint32_t ref = 0;     // sender's id, echoed back in the Outcome
  // Optional hints for characters that stage cards in their own style (others
  // ignore them): what kind of news it is ("done", "error", "remind", ...) and
  // the face to show; ASCII, "" = up to the character
  char tag[9] = "";
  char face[9] = "";
  char title[49] = "";
  char body[193] = "";
  uint8_t optionCount = 0;  // prompts only
  char options[kMaxOptions][25] = {};
};

struct CharacterState {
  Mood mood = Mood::Idle;
  uint32_t moodSinceMs = 0;
};

struct CardState {
  uint32_t id = 0;  // increments per shown card; 0 = nothing shown yet
  bool visible = false;
  uint32_t changedMs = 0;  // last show/hide time, drives the slide animation
  int8_t chosen = -1;      // prompt option picked, for press feedback
  CardContent content;
};

struct WorldState {
  uint32_t timeMs = 0;  // engine time this snapshot was taken
  CharacterState character;
  CardState card;
  uint16_t queued = 0;
};

// Reported back to whoever sent a prompt
struct Outcome {
  uint32_t ref = 0;
  int8_t option = -1;  // -1 = expired without an answer
};

}  // namespace engine
