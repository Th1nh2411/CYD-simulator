#pragma once

#include <cstddef>
#include <cstdint>

#include "app/gfx/canvas.h"
#include "hal/display.h"

namespace gfx {

class Compositor;

// Something drawn on screen. Layers report the area they occupy; the
// compositor redraws only areas that changed, compositing every layer that
// overlaps them back to front.
class Layer {
 public:
  virtual ~Layer() = default;
  const Rect& bounds() const { return bounds_; }
  bool visible() const { return visible_; }
  int z() const { return z_; }
  void setVisible(bool v);

 protected:
  // Draw into c; only c.area() needs to be correct
  virtual void draw(Canvas& c) const = 0;
  void setBounds(const Rect& r);  // marks old and new areas dirty
  void invalidate();              // content changed within current bounds
  void invalidate(const Rect& r);  // content changed in part of the bounds

 private:
  friend class Compositor;
  Compositor* owner_ = nullptr;
  Rect bounds_;
  bool visible_ = true;
  int z_ = 0;
};

// Dirty-rectangle compositor rendering into two strip buffers alternately,
// so the CPU draws one strip while the display transfers the other.
// The first layer must be opaque and cover the whole screen.
class Compositor {
 public:
  bool begin(hal::Display* display, Color* buf0, Color* buf1, size_t bufPixels);
  // Higher z draws on top; equal z keeps insertion order
  void add(Layer* layer, int z);
  void invalidate(const Rect& r);
  void invalidateAll() { invalidate(screen_); }
  bool dirty() const { return dirtyCount_ > 0; }
  // Draws and sends all dirty areas; returns pixels sent (0 if nothing changed)
  uint32_t render();

 private:
  static constexpr int kMaxLayers = 24;
  static constexpr int kMaxDirty = 8;
  static constexpr int kMergeSlack = 1024;  // px of overdraw accepted to merge two rects

  hal::Display* display_ = nullptr;
  Color* bufs_[2] = {};
  size_t bufPixels_ = 0;
  int nextBuf_ = 0;
  Rect screen_;
  Canvas canvas_;
  Layer* layers_[kMaxLayers] = {};
  int layerCount_ = 0;
  Rect dirty_[kMaxDirty];
  int dirtyCount_ = 0;
};

}  // namespace gfx
