#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "driver/spi_master.h"
#include "drivers/panels/axs15231b.h"
#include "esp_async_memcpy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/display.h"

namespace drivers {

// AXS15231B panel on quad SPI (ESP-IDF spi_master), shown landscape or, turned
// (quarterTurns), portrait. ESP32-S3: needs GDMA access to PSRAM.
// Over QSPI the controller has no row window: a write starts at the top row
// (RAMWR) or continues where the previous one stopped (RAMWRC), and partial
// frames left stale pieces on screen. So every frame is sent whole, from one of
// two frames in PSRAM kept in panel (portrait) order. The CPU never touches the
// frames (writing PSRAM through the cache was the slow part); GDMA moves them:
//   flush()   UI core: landscape, gets full-height column strips
//             (prefersColumnStrips) and turns 16 columns at a time into whole
//             panel rows in internal RAM; portrait, gets full-width row strips
//             (prefersFullRows), already panel rows (upside down: reversed);
//             GDMA copies each block into the back frame
//   present() UI core: has GDMA bring over the rows the last frame changed and
//             this one did not (the back frame still holds the one before),
//             waits until the last frame is on the panel (so the UI never draws
//             faster than the panel shows), swaps the frames, wakes the sender
//   sender    other core: waits until the panel starts a scan (TE line falls),
//             then GDMA copies 16-row chunks of the front frame into two internal
//             buffers in turn and SPI DMA sends them
// No tearing: TE is high for the V-blank (2.7 ms) and low while the panel scans
// its memory (13.9 ms). A frame takes 16 ms at 40 MHz, longer than the scan, so
// sent as a scan starts it trails that scan to the last row: the panel shows the
// previous frame whole, then this one. (Sent from the V-blank it would need to
// beat the scan to the last row; 80 MHz could, but garbles pixels on this board.)
// (spi_master in IDF 4.4 cannot DMA from PSRAM itself, hence the GDMA copies.)
class Axs15231bQspiDisplay : public hal::Display {
 public:
  struct Config {
    const panels::Axs15231bInitCmd* init = nullptr;
    uint8_t initCount = 0;
    int spiHost = 1;  // 1 = SPI2_HOST
    int sclk = -1, cs = -1;
    int data[4] = {-1, -1, -1, -1};
    int te = -1;  // tearing-effect line; without it frames start whenever they are ready
    uint32_t pclkHz = 40000000;
    int16_t panelWidth = 320, panelHeight = 480;  // controller orientation (portrait)
    // Quarter turns clockwise from landscape, screen (x, y) -> (h-1-y, x) a turn:
    // 0 landscape, 1 portrait (the panel's own), 2 and 3 the same upside down
    int quarterTurns = 0;
    bool mirror = false;  // then left to right (a prism shows the picture mirrored)
    int chunkRows = 16;  // panel rows per SPI transfer (two internal buffers of this size)
    int senderCore = 0;  // the UI draws on the other one
  };

  bool begin(const Config& cfg);

  int width() const override { return portrait_ ? cfg_.panelWidth : cfg_.panelHeight; }
  int height() const override { return portrait_ ? cfg_.panelHeight : cfg_.panelWidth; }
  gfx::Color* allocBuffer(size_t pixels) override;
  void flush(const gfx::Rect& area, const gfx::Color* pixels) override;
  void present() override;
  void waitIdle() override;
  // Landscape, a screen column is a panel row: full-height column strips are
  // whole rows of the frame; portrait, full-width row strips are
  bool prefersColumnStrips() const override { return !portrait_; }
  bool prefersFullRows() const override { return portrait_; }

 private:
  static constexpr int kStageCols = 16;  // panel rows per GDMA block (landscape: screen columns)
  static constexpr uint8_t kDrawn = 1;    // row flushed since the last present()
  static constexpr uint8_t kStale = 2;    // row newer in the front frame than in the back one

  static void senderTask(void* self);
  static bool onCopied(async_memcpy_t, async_memcpy_event_t*, void* sem);
  static void onVsync(void* sem);
  void sendFrame(const gfx::Color* frame);
  void copyRows(gfx::Color* dst, const gfx::Color* src, int first, int end, int& queued);
  void command(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);

  Config cfg_;
  bool portrait_ = false;  // odd quarterTurns
  bool rowsReversed_ = false, colsReversed_ = false;  // panel rows in reverse order / each backwards
  spi_device_handle_t dev_ = nullptr;
  async_memcpy_t reader_ = nullptr, writer_ = nullptr;  // frame -> SPI buffers; blocks, rows -> frame
  SemaphoreHandle_t read_ = nullptr;
  SemaphoreHandle_t staged_[2] = {};  // given while the block buffer is free
  SemaphoreHandle_t copied_ = nullptr;  // counts finished row copies between the frames
  SemaphoreHandle_t idle_ = nullptr;    // given while no frame is waiting or being sent
  SemaphoreHandle_t vsync_ = nullptr;   // given as each scan starts (TE line)
  TaskHandle_t sender_ = nullptr;
  gfx::Color* frames_[2] = {};  // panel orientation, row-major, in PSRAM
  std::atomic<int> front_{0};   // the frame the panel shows; the UI draws into the other
  gfx::Color* dma_[2] = {};     // SPI chunks
  gfx::Color* stage_[2] = {};   // turned blocks on their way into the back frame
  spi_transaction_t trans_[2] = {};
  int nextStage_ = 0;
  bool changed_ = false;         // UI task: flushed since the last present()
  std::vector<uint8_t> rows_;    // UI task: per panel row, kDrawn | kStale
};

}  // namespace drivers
