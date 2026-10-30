#pragma once

#include <cstddef>

#include "app/gfx/color.h"
#include "app/gfx/geometry.h"

namespace hal {

class Display {
 public:
  virtual ~Display() = default;

  virtual int width() const = 0;
  virtual int height() const = 0;

  // Allocates a pixel buffer usable with flush() (e.g. DMA-capable memory).
  virtual gfx::Color* allocBuffer(size_t pixels) = 0;

  // Sends area.w * area.h row-major pixels. May return before the transfer
  // finishes: the buffer must stay untouched until the next flush() or
  // waitIdle() call has returned.
  virtual void flush(const gfx::Rect& area, const gfx::Color* pixels) = 0;
  // After the last flush() of a frame. Panels that cannot take a partial
  // window keep a frame copy and send it from here.
  virtual void present() {}
  // Asks the compositor for strips a few columns wide and the full screen
  // height (changed columns are redrawn top to bottom): a panel that stores the
  // picture turned then gets whole rows of its own, one block per strip,
  // instead of a short piece of every row.
  virtual bool prefersColumnStrips() const { return false; }
  // Row strips the full screen width (changed rows are redrawn edge to edge):
  // a panel that keeps a frame in its own row order then copies whole rows.
  virtual bool prefersFullRows() const { return false; }
  virtual void waitIdle() = 0;
};

}  // namespace hal
