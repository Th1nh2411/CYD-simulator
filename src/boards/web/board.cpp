// Browser simulator (tools/web/sim.py): a board's screen drawn into a canvas;
// mouse or finger on it is the touch panel. The CYD2USB screen (320x240
// landscape) unless the page URL picks another board with ?board=jc3248w535
// or ?board=muma. Turned, the canvas is turned: the pointer is already in
// screen coordinates. A mirror is the panel's business: the canvas shows the
// picture as drawn.

#include <emscripten/emscripten.h>

#include "drivers/web/canvas_display.h"
#include "drivers/web/pointer_touch.h"
#include "drivers/web/sim_imu.h"
#include "hal/board.h"

namespace {
drivers::CanvasDisplay display;
drivers::PointerTouch touch;
drivers::SimImu imu;  // keys on the page tilt and shake it
hal::Board board;

EM_JS(int, boardFromUrl, (), {
  const name = new URLSearchParams(location.search).get('board');
  return name === 'jc3248w535' ? 1 : name === 'muma' ? 2 : 0;
});
}  // namespace

hal::Board& hal::initBoard(int quarterTurns, bool) {
  const int which = boardFromUrl();
  int w = 320, h = 240;
  if (which == 1) {
    w = 480;  // JC3248W535: 3.5" 320x480 panel turned landscape
    h = 320;
    board.name = "Web simulator (JC3248W535 screen)";
  } else if (which == 2) {
    w = h = 240;  // Spotpear muma: 1.54" square panel
    board.name = "Web simulator (muma screen)";
  } else {
    board.name = "Web simulator (CYD2USB screen)";
  }
  if (quarterTurns & 1) display.begin(h, w);
  else display.begin(w, h);
  board.display = &display;
  board.touch = &touch;
  board.imu = &imu;
  return board;
}
