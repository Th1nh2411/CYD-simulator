#pragma once

#include <cstddef>
#include <cstdint>

// Small sound cues synthesized live, and short voice clips from flash. Mono
// 16-bit at kSampleRate; the platform's audio task pulls samples with render()
// and hands them to its speaker. Portable: no audio API here.
namespace audio {

constexpr int kSampleRate = 16000;

enum class Cue : uint8_t {
  None,
  Blip,          // one letter of her talking
  Tick,          // a quiet click: a small notification
  Alert,         // two high-low beeps, twice: she needs you
  Done,          // a rising arpeggio
  Error,         // a falling buzz with noise
  Glitch,        // crackle: she glitches or teleports
  Clip,          // a recorded clip (Sound::clip)
  Count
};

// Recorded samples at kSampleRate, in flash
struct Clip {
  const int16_t* pcm;
  uint32_t length;
};

// A cue and how loud, 0 .. 100
struct Sound {
  Cue cue = Cue::None;
  uint8_t volume = 60;
  const Clip* clip = nullptr;  // Cue::Clip only
};

// Plays cues over each other (a few notes at once and one clip). One thread.
class Synth {
 public:
  void play(const Sound& s);
  bool active() const;
  // Fills out with the next n samples (silence once nothing plays)
  void render(int16_t* out, size_t n);

 private:
  enum class Wave : uint8_t { Square, Triangle, Saw, Noise };
  struct Note {
    Wave wave = Wave::Square;
    uint32_t start = 0, length = 0;  // samples, start relative to the synth clock
    float freq = 0, freqEnd = 0, gain = 0, phase = 0;
  };
  static constexpr int kNotes = 12;

  void note(Wave w, float freq, float freqEnd, uint32_t delayMs, uint32_t lengthMs, float gain);
  float noise();

  Note notes_[kNotes];
  const int16_t* clip_ = nullptr;
  uint32_t clipLeft_ = 0;
  float clipGain_ = 0;
  uint32_t clock_ = 0, rng_ = 0x2545F491u;
  float volume_ = 0.6f;
};

}  // namespace audio
