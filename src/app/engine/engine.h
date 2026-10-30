#pragma once

#include <cstdint>

#include "app/core/spsc_queue.h"
#include "app/core/triple_buffer.h"
#include "app/engine/events.h"
#include "app/engine/world.h"

namespace engine {

// Character mood and the card queue. Runs on its own thread at a fixed tick;
// talks to the rest of the system only through lock-free queues (events in,
// outcomes out) and a triple-buffered WorldState snapshot.
class Engine {
 public:
  // Each source must post from a single thread
  enum Source : uint8_t { kFromUi, kFromSystem, kFromNetwork, kSourceCount };

  void seed(uint32_t s) { rng_ = s ? s : 0x9E3779B9u; }

  // Any thread (one per source)
  bool post(Source src, const Event& e) { return inbox_[src].push(e); }

  // Engine thread
  void tick(uint32_t nowMs);

  // Reader thread: pollState() swaps in the newest snapshot, state() reads it
  bool pollState() { return out_.update(); }
  const WorldState& state() const { return out_.front(); }

  // Single consumer thread: answers to prompts
  bool pollOutcome(Outcome& o) { return outcomes_.pop(o); }

 private:
  static constexpr int kMaxQueued = 6;

  void handle(const Event& e, uint32_t now);
  void step(uint32_t now);
  void enterMood(Mood m, uint32_t now);
  void enqueue(const CardContent& c);
  void showNext(uint32_t now);
  void hideCard(uint32_t now);
  void answer(int8_t option, uint32_t now);

  core::SpscQueue<Event, 8> inbox_[kSourceCount];
  core::SpscQueue<Outcome, 8> outcomes_;
  core::TripleBuffer<WorldState> out_;

  WorldState world_;
  CardContent queue_[kMaxQueued];
  int queueCount_ = 0;
  uint32_t hideAtMs_ = 0;  // deferred hide after a prompt answer (press feedback)
  bool hidePending_ = false;
  bool started_ = false;
  uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace engine
