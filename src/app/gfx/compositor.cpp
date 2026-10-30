#include "app/gfx/compositor.h"

#include <algorithm>
#include <climits>

namespace gfx {

void Layer::setVisible(bool v) {
  if (v == visible_) return;
  if (owner_) owner_->invalidate(bounds_);
  visible_ = v;
}

void Layer::setBounds(const Rect& r) {
  if (r == bounds_) return;
  if (owner_ && visible_) {
    owner_->invalidate(bounds_);
    owner_->invalidate(r);
  }
  bounds_ = r;
}

void Layer::invalidate() {
  if (owner_ && visible_) owner_->invalidate(bounds_);
}

void Layer::invalidate(const Rect& r) {
  if (owner_ && visible_) owner_->invalidate(r.intersect(bounds_));
}

bool Compositor::begin(hal::Display* display, Color* buf0, Color* buf1, size_t bufPixels) {
  if (!display || !buf0 || !buf1 || bufPixels < size_t(display->width())) return false;
  display_ = display;
  bufs_[0] = buf0;
  bufs_[1] = buf1;
  bufPixels_ = bufPixels;
  screen_ = Rect(0, 0, display->width(), display->height());
  invalidateAll();
  return true;
}

void Compositor::add(Layer* layer, int z) {
  if (layerCount_ >= kMaxLayers) return;
  layer->owner_ = this;
  layer->z_ = z;
  int i = layerCount_++;
  for (; i > 0 && layers_[i - 1]->z_ > z; i--) layers_[i] = layers_[i - 1];
  layers_[i] = layer;
  layer->invalidate();
}

void Compositor::invalidate(const Rect& area) {
  Rect r = area.intersect(screen_);
  if (r.empty()) return;

  // Absorb every pending rect that merges cheaply; growth can enable more merges
  for (bool merged = true; merged;) {
    merged = false;
    for (int i = 0; i < dirtyCount_; i++) {
      Rect u = dirty_[i].unite(r);
      if (u.area() <= dirty_[i].area() + r.area() + kMergeSlack) {
        r = u;
        dirty_[i] = dirty_[--dirtyCount_];
        merged = true;
        break;
      }
    }
  }
  if (dirtyCount_ < kMaxDirty) {
    dirty_[dirtyCount_++] = r;
    return;
  }

  // Out of slots: grow the rect that needs the least extra area
  int best = 0, bestGrowth = INT_MAX;
  for (int i = 0; i < dirtyCount_; i++) {
    int growth = dirty_[i].unite(r).area() - dirty_[i].area();
    if (growth < bestGrowth) {
      bestGrowth = growth;
      best = i;
    }
  }
  dirty_[best] = dirty_[best].unite(r);
}

uint32_t Compositor::render() {
  if (!dirtyCount_) return 0;

  // Mostly dirty: one full-screen pass beats many partial ones
  int total = 0;
  for (int i = 0; i < dirtyCount_; i++) total += dirty_[i].area();
  if (total * 4 > screen_.area() * 3) {
    dirty_[0] = screen_;
    dirtyCount_ = 1;
  }

  uint32_t sent = 0;
  const bool columns = display_->prefersColumnStrips();
  const bool fullRows = !columns && display_->prefersFullRows();
  if (columns || fullRows) {
    // Changed columns are redrawn top to bottom (or rows edge to edge);
    // overlapping spans merge so nothing is drawn twice
    for (int i = 0; i < dirtyCount_; i++) {
      const Rect& d = dirty_[i];
      dirty_[i] = columns ? Rect(d.x, screen_.y, d.w, screen_.h) : Rect(screen_.x, d.y, screen_.w, d.h);
    }
    std::sort(dirty_, dirty_ + dirtyCount_,
              [columns](const Rect& a, const Rect& b) { return columns ? a.x < b.x : a.y < b.y; });
    int n = 0;
    for (int i = 0; i < dirtyCount_; i++) {
      if (n > 0 && (columns ? dirty_[i].x <= dirty_[n - 1].right() : dirty_[i].y <= dirty_[n - 1].bottom())) {
        dirty_[n - 1] = dirty_[n - 1].unite(dirty_[i]);
      } else {
        dirty_[n++] = dirty_[i];
      }
    }
    dirtyCount_ = n;
  }
  for (int d = 0; d < dirtyCount_; d++) {
    const Rect r = dirty_[d];
    // Rows across the area, or (if the display asks) columns down it
    int step = int(bufPixels_ / size_t(columns ? r.h : r.w));
    for (int pos = columns ? r.x : r.y; pos < (columns ? r.right() : r.bottom()); pos += step) {
      Rect strip = columns ? Rect(pos, r.y, std::min(step, r.right() - pos), r.h)
                           : Rect(r.x, pos, r.w, std::min(step, r.bottom() - pos));
      Color* buf = bufs_[nextBuf_];
      nextBuf_ ^= 1;  // keep alternating across frames: the other buffer may still be in flight

      canvas_.reset(buf, strip);
      for (int i = 0; i < layerCount_; i++) {
        const Layer* l = layers_[i];
        if (l->visible_ && l->bounds_.intersects(strip)) l->draw(canvas_);
      }
      display_->flush(strip, buf);
      sent += uint32_t(strip.area());
    }
  }
  display_->present();
  dirtyCount_ = 0;
  return sent;
}

}  // namespace gfx
