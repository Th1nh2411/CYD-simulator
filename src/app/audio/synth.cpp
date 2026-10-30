#include "app/audio/synth.h"

#include <cmath>

namespace audio {

namespace {

float midi(int note) { return 440.0f * powf(2.0f, float(note - 69) / 12.0f); }

uint32_t samples(uint32_t ms) { return ms * uint32_t(kSampleRate) / 1000; }

// Soft clip, so that notes over each other do not wrap around
float soft(float x) { return x / (1.0f + fabsf(x)); }

}  // namespace

void Synth::note(Wave w, float freq, float freqEnd, uint32_t delayMs, uint32_t lengthMs, float gain) {
  // A free slot, or the one closest to its end
  int pick = 0;
  uint32_t leastLeft = UINT32_MAX;
  for (int i = 0; i < kNotes; i++) {
    const Note& n = notes_[i];
    const uint32_t end = n.start + n.length;
    const uint32_t left = int32_t(end - clock_) > 0 ? end - clock_ : 0;
    if (left < leastLeft) {
      leastLeft = left;
      pick = i;
    }
  }
  Note& n = notes_[pick];
  n.wave = w;
  n.start = clock_ + samples(delayMs);
  n.length = samples(lengthMs);
  n.freq = freq;
  n.freqEnd = freqEnd;
  n.gain = gain * volume_;
  n.phase = 0;
}

float Synth::noise() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(int32_t(rng_)) / 2147483648.0f;
}

// Recipes after capri-desktop's Web Audio voice (src/renderer/js/voice.js)
void Synth::play(const Sound& s) {
  volume_ = float(s.volume > 100 ? 100 : s.volume) / 100.0f;
  switch (s.cue) {
    case Cue::Blip: {
      static const int kScale[8] = {62, 65, 67, 69, 72, 74, 77, 79};
      const float f = midi(kScale[(rng_ >> 8) & 7]);
      noise();
      note(Wave::Square, f, f, 0, 32, 0.10f);
      note(Wave::Triangle, f * 1.5f, f * 1.5f, 0, 32, 0.08f);
      note(Wave::Square, f * 0.5f, f * 0.5f, 0, 32, 0.05f);
      break;
    }
    case Cue::Tick:
      note(Wave::Square, 1760, 1760, 0, 14, 0.12f);
      note(Wave::Triangle, 2637, 2637, 18, 12, 0.10f);
      break;
    case Cue::Alert:
      for (int k = 0; k < 2; k++) {
        note(Wave::Square, 988, 988, k * 260, 100, 0.16f);
        note(Wave::Square, 740, 740, k * 260 + 120, 100, 0.16f);
      }
      break;
    case Cue::Done: {
      static const int kArp[5] = {64, 68, 71, 75, 76};
      for (int k = 0; k < 5; k++) {
        note(Wave::Triangle, midi(kArp[k] + 12), midi(kArp[k] + 12), k * 70, k == 4 ? 260 : 90, 0.22f);
        note(Wave::Square, midi(kArp[k]), midi(kArp[k]), k * 70, k == 4 ? 260 : 90, 0.06f);
      }
      break;
    }
    case Cue::Error:
      note(Wave::Saw, 110, 55, 0, 320, 0.22f);
      note(Wave::Noise, 0, 0, 0, 120, 0.12f);
      break;
    case Cue::Glitch:
      for (int k = 0; k < 3; k++) note(Wave::Noise, 0, 0, k * 45, 22, 0.10f);
      note(Wave::Square, 3000, 600, 0, 90, 0.05f);
      break;
    case Cue::Clip:
      if (!s.clip) break;
      clip_ = s.clip->pcm;
      clipLeft_ = s.clip->length;
      clipGain_ = volume_;
      break;
    case Cue::None:
    case Cue::Count:
      break;
  }
}

bool Synth::active() const {
  if (clipLeft_) return true;
  for (const Note& n : notes_) {
    if (int32_t(n.start + n.length - clock_) > 0) return true;
  }
  return false;
}

void Synth::render(int16_t* out, size_t count) {
  constexpr float kDt = 1.0f / kSampleRate;
  for (size_t i = 0; i < count; i++, clock_++) {
    float v = 0;
    for (Note& n : notes_) {
      const uint32_t t = clock_ - n.start;
      if (int32_t(clock_ - n.start) < 0 || t >= n.length) continue;
      const float u = float(t) / float(n.length);
      const float f = n.freq + (n.freqEnd - n.freq) * u;
      n.phase += f * kDt;
      n.phase -= floorf(n.phase);
      float w;
      switch (n.wave) {
        case Wave::Square: w = n.phase < 0.5f ? 1.0f : -1.0f; break;
        case Wave::Triangle: w = 4.0f * fabsf(n.phase - 0.5f) - 1.0f; break;
        case Wave::Saw: w = 2.0f * n.phase - 1.0f; break;
        default: w = noise(); break;
      }
      // 3 ms attack, then a decay to nothing at the end
      const float attack = std::fmin(1.0f, float(t) / (0.003f * kSampleRate));
      v += w * n.gain * attack * (1.0f - u) * (1.0f - u);
    }
    if (clipLeft_) {
      v += float(*clip_++) / 32768.0f * clipGain_;
      clipLeft_--;
    }
    out[i] = int16_t(soft(v * 1.4f) * 30000.0f);
  }
}

}  // namespace audio
