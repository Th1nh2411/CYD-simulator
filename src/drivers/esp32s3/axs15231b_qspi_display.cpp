#include "drivers/esp32s3/axs15231b_qspi_display.h"

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "esp32s3/rom/cache.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace drivers {

namespace {
const char* TAG = "axs15231b";

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }

// QSPI framing: an opcode, then the DCS command as the middle byte of a 24-bit
// address, both on one line; pixel data goes on four lines
constexpr uint8_t kOpWriteCmd = 0x02, kOpWritePixels = 0x32;
constexpr uint8_t kSwReset = 0x01, kSleepOut = 0x11, kDisplayOn = 0x29, kColSet = 0x2A, kRowSet = 0x2B;
constexpr uint8_t kMemWrite = 0x2C, kMemWriteContinue = 0x3C, kMadctl = 0x36, kColmod = 0x3A;
constexpr uint8_t kTearOn = 0x35;
constexpr size_t kPsramAlign = 64;  // GDMA block size for PSRAM: chunk addresses and sizes are multiples

// The inner loops are kept to a handful of variables: with more, Xtensa GCC
// spills to the stack on every pixel and the turn gets slower than plain copies.

// Two screen columns (32-bit words, rows `stride` words apart) into two panel
// rows of `pairs` words, two screen rows per step. Screen row y lands at panel
// column pw-1-y: the rows fill from their end.
void turnPair(const uint32_t* __restrict s, int stride, uint32_t* __restrict r0, uint32_t* __restrict r1,
              int pairs) {
  r0 += pairs;
  r1 += pairs;
  for (int k = pairs; k > 0; k--, s += 2 * stride) {
    uint32_t a = s[0], b = s[stride];  // rows y and y+1: (left column, right column << 16)
    *--r0 = (b & 0xFFFF) | (a << 16);
    *--r1 = (b >> 16) | (a & 0xFFFF0000);
  }
}

// Same with flip: screen row y lands at panel column y
void turnPairFlipped(const uint32_t* __restrict s, int stride, uint32_t* __restrict r0, uint32_t* __restrict r1,
                     int pairs) {
  for (int k = pairs; k > 0; k--, s += 2 * stride) {
    uint32_t a = s[0], b = s[stride];
    *r0++ = (a & 0xFFFF) | (b << 16);
    *r1++ = (a >> 16) | (b & 0xFFFF0000);
  }
}

// One screen column (pixels `stride` apart) into one panel row of pw pixels
void turnOne(const gfx::Color* __restrict s, int stride, gfx::Color* __restrict r, int pw, bool flip) {
  if (!flip) {
    r += pw;
    for (int k = pw; k > 0; k--, s += stride) *--r = *s;
  } else {
    for (int k = pw; k > 0; k--, s += stride) *r++ = *s;
  }
}

// n screen columns (pw rows, srcW pixels per row) into n panel rows: with
// reverseRows the panel rows come in reverse, with reverseCols screen row y lands
// at panel column y (else pw-1-y). Pairs of columns go 32 bits at a time when
// the strip keeps them aligned.
void turnColumns(const gfx::Color* src, int srcW, int n, int pw, bool reverseRows, bool reverseCols,
                 gfx::Color* stage) {
  auto row = [&](int i) { return stage + size_t(reverseRows ? n - 1 - i : i) * pw; };
  int i = 0;
  if ((srcW & 1) == 0 && (uintptr_t(src) & 3) == 0) {
    for (; i + 1 < n; i += 2) {
      auto s = reinterpret_cast<const uint32_t*>(src + i);
      auto r0 = reinterpret_cast<uint32_t*>(row(i)), r1 = reinterpret_cast<uint32_t*>(row(i + 1));
      if (!reverseCols) turnPair(s, srcW / 2, r0, r1, pw / 2);
      else turnPairFlipped(s, srcW / 2, r0, r1, pw / 2);
    }
  }
  for (; i < n; i++) turnOne(src + i, srcW, row(i), pw, reverseCols);
}

// One row backwards, two pixels at a time when both rows are word aligned
void reverseRow(const gfx::Color* __restrict s, gfx::Color* __restrict d, int pw) {
  if ((pw & 1) == 0 && ((uintptr_t(s) | uintptr_t(d)) & 3) == 0) {
    auto s32 = reinterpret_cast<const uint32_t*>(s);
    auto d32 = reinterpret_cast<uint32_t*>(d) + pw / 2;
    for (int k = pw / 2; k > 0; k--) {
      uint32_t v = *s32++;
      *--d32 = (v >> 16) | (v << 16);
    }
    return;
  }
  d += pw;
  for (int k = pw; k > 0; k--) *--d = *s++;
}

// n screen rows (portrait: pw pixels, the panel's own rows) into n panel rows:
// with reverseRows they come in reverse, with reverseCols each runs backwards
void copyRowsIn(const gfx::Color* src, int n, int pw, bool reverseRows, bool reverseCols, gfx::Color* stage) {
  if (!reverseRows && !reverseCols) {
    memcpy(stage, src, size_t(n) * pw * sizeof(gfx::Color));
    return;
  }
  for (int i = 0; i < n; i++) {
    const gfx::Color* s = src + size_t(i) * pw;
    gfx::Color* d = stage + size_t(reverseRows ? n - 1 - i : i) * pw;
    if (reverseCols) reverseRow(s, d, pw);
    else memcpy(d, s, size_t(pw) * sizeof(gfx::Color));
  }
}
}  // namespace

bool Axs15231bQspiDisplay::begin(const Config& cfg) {
  cfg_ = cfg;
  portrait_ = (cfg.quarterTurns & 1) != 0;
  // Upside down reverses both; a mirror reverses the screen's x, which runs
  // along panel rows' order in landscape and along each row in portrait
  const bool flip = (cfg.quarterTurns & 3) >= 2;
  rowsReversed_ = flip != (cfg.mirror && !portrait_);
  colsReversed_ = flip != (cfg.mirror && portrait_);
  const size_t rowBytes = size_t(cfg.panelWidth) * sizeof(gfx::Color);
  const size_t chunkBytes = rowBytes * cfg.chunkRows, frameBytes = rowBytes * cfg.panelHeight;

  spi_bus_config_t bus = {};
  bus.data0_io_num = cfg.data[0];
  bus.data1_io_num = cfg.data[1];
  bus.data2_io_num = cfg.data[2];
  bus.data3_io_num = cfg.data[3];
  bus.sclk_io_num = cfg.sclk;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = int(chunkBytes);
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  esp_err_t err = spi_bus_initialize(spi_host_device_t(cfg.spiHost), &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return false;
  }

  spi_device_interface_config_t dev = {};
  dev.command_bits = 8;
  dev.address_bits = 24;
  dev.mode = 3;
  dev.clock_speed_hz = int(cfg.pclkHz);
  dev.spics_io_num = cfg.cs;
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = 2;
  err = spi_bus_add_device(spi_host_device_t(cfg.spiHost), &dev, &dev_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    return false;
  }

  if (rowBytes % kPsramAlign != 0) {
    ESP_LOGE(TAG, "panel rows must be a multiple of %u bytes for GDMA", unsigned(kPsramAlign));
    return false;
  }
  for (auto& f : frames_) f = static_cast<gfx::Color*>(heap_caps_aligned_alloc(kPsramAlign, frameBytes, MALLOC_CAP_SPIRAM));
  for (auto& d : dma_) d = static_cast<gfx::Color*>(heap_caps_malloc(chunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  for (auto& b : stage_) {
    b = static_cast<gfx::Color*>(heap_caps_malloc(rowBytes * kStageCols, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  }
  rows_.assign(size_t(cfg.panelHeight), 0);
  read_ = xSemaphoreCreateBinary();
  vsync_ = xSemaphoreCreateBinary();
  copied_ = xSemaphoreCreateCounting(UBaseType_t(cfg.panelHeight), 0);
  for (SemaphoreHandle_t* sem : {&staged_[0], &staged_[1], &idle_}) {
    *sem = xSemaphoreCreateBinary();
    if (*sem) xSemaphoreGive(*sem);
  }
  async_memcpy_config_t gdma = ASYNC_MEMCPY_DEFAULT_CONFIG();
  gdma.psram_trans_align = kPsramAlign;
  gdma.sram_trans_align = 4;
  bool readerOk = esp_async_memcpy_install(&gdma, &reader_) == ESP_OK;
  // The writer may copy nearly a whole frame of rows at once (after a full
  // redraw); a copy needs one descriptor per 4 KB
  gdma.backlog = uint32_t(frameBytes / 4000 + 2 * (rowBytes * kStageCols / 4000 + 1) + 4);
  bool writerOk = esp_async_memcpy_install(&gdma, &writer_) == ESP_OK;
  if (!frames_[0] || !frames_[1] || !dma_[0] || !dma_[1] || !stage_[0] || !stage_[1] || !read_ || !vsync_ ||
      !staged_[0] || !staged_[1] || !copied_ || !idle_ || !readerOk || !writerOk) {
    ESP_LOGE(TAG, "no memory for the frames (PSRAM), DMA buffers or GDMA channels");
    return false;
  }
  // Black, and out of the cache: from here on only GDMA reads and writes the frames
  for (gfx::Color* f : frames_) {
    memset(f, 0, frameBytes);
    Cache_WriteBack_Addr(uint32_t(f), frameBytes);
  }

  command(kSwReset);
  sleepMs(120);
  command(kSleepOut);
  sleepMs(100);
  const uint8_t madctl = 0x00, colmod = 0x55;  // panel orientation, RGB565
  command(kMadctl, &madctl, 1);
  command(kColmod, &colmod, 1);
  for (uint8_t i = 0; i < cfg.initCount; i++) {
    const panels::Axs15231bInitCmd& c = cfg.init[i];
    command(c.cmd, c.data, c.len);
    if (c.delayMs) sleepMs(c.delayMs);
  }
  const int16_t lastCol = cfg.panelWidth - 1, lastRow = cfg.panelHeight - 1;
  const uint8_t cols[4] = {0, 0, uint8_t(lastCol >> 8), uint8_t(lastCol)};
  const uint8_t rows[4] = {0, 0, uint8_t(lastRow >> 8), uint8_t(lastRow)};
  command(kColSet, cols, 4);
  command(kRowSet, rows, 4);
  if (cfg.te >= 0) {
    const uint8_t vblankOnly = 0x00;
    command(kTearOn, &vblankOnly, 1);
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << cfg.te;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;  // TE falls as the panel starts a scan
    gpio_config(&io);
    gpio_install_isr_service(0);  // fails harmlessly if already installed
    gpio_isr_handler_add(gpio_num_t(cfg.te), onVsync, vsync_);
  }
  command(kDisplayOn);
  sleepMs(20);

  if (xTaskCreatePinnedToCore(senderTask, "lcd", 3072, this, 2, &sender_, cfg.senderCore) != pdPASS) {
    ESP_LOGE(TAG, "no sender task");
    return false;
  }
  ESP_LOGI(TAG, "%dx%d (%s) @ %u Hz", width(), height(), portrait_ ? "portrait" : "landscape", unsigned(cfg.pclkHz));
  return true;
}

gfx::Color* Axs15231bQspiDisplay::allocBuffer(size_t pixels) {
  // Strips are copied into the stage buffers, so fast internal RAM rather than DMA-capable
  return static_cast<gfx::Color*>(heap_caps_malloc(pixels * sizeof(gfx::Color), MALLOC_CAP_INTERNAL));
}

void Axs15231bQspiDisplay::flush(const gfx::Rect& a, const gfx::Color* pixels) {
  const int pw = cfg_.panelWidth, ph = cfg_.panelHeight;
  // Only GDMA may write the frames, and only whole panel rows
  if (portrait_ ? (a.x != 0 || a.w != pw) : (a.y != 0 || a.h != pw)) {
    static bool reported = false;
    if (!reported) {
      ESP_LOGE(TAG, "flush needs full-%s strips, got %dx%d at %d,%d", portrait_ ? "width row" : "height column",
               a.w, a.h, a.x, a.y);
    }
    reported = true;
    return;
  }
  gfx::Color* back = frames_[front_ ^ 1];
  // Landscape, screen column x is panel row x (reversed: the mirror) and screen
  // row y is panel column pw-1-y (reversed: y), so a block of columns turns into
  // whole panel rows. Portrait, screen row y is panel row y (reversed: the
  // mirror) and runs along it (reversed: backwards).
  const int first = portrait_ ? a.y : a.x, count = portrait_ ? a.h : a.w;
  for (int i0 = 0; i0 < count; i0 += kStageCols) {
    const int n = std::min(kStageCols, count - i0), b = nextStage_;
    nextStage_ ^= 1;
    xSemaphoreTake(staged_[b], portMAX_DELAY);  // its previous block has reached the frame
    if (portrait_) copyRowsIn(pixels + size_t(i0) * pw, n, pw, rowsReversed_, colsReversed_, stage_[b]);
    else turnColumns(pixels + i0, a.w, n, pw, rowsReversed_, colsReversed_, stage_[b]);
    const int firstRow = rowsReversed_ ? ph - (first + i0 + n) : first + i0;
    if (esp_async_memcpy(writer_, back + size_t(firstRow) * pw, stage_[b], size_t(n) * pw * sizeof(gfx::Color),
                         onCopied, staged_[b]) != ESP_OK) {
      xSemaphoreGive(staged_[b]);  // refused (logged by the driver): lose the block, not the UI
    }
  }
  const int lo = rowsReversed_ ? ph - first - count : first;
  for (int r = lo; r < lo + count; r++) rows_[r] |= kDrawn;
  changed_ = true;
}

void Axs15231bQspiDisplay::present() {
  if (!changed_) return;
  changed_ = false;
  const int ph = cfg_.panelHeight, front = front_;
  // The back frame still holds the frame before the front one: bring over the rows
  // the front one changed that this frame left alone (queued behind its blocks)
  int queued = 0;
  for (int r = 0; r < ph; r++) {
    if (rows_[r] != kStale) continue;
    int end = r + 1;
    while (end < ph && rows_[end] == kStale) end++;
    copyRows(frames_[front ^ 1], frames_[front], r, end, queued);
    r = end;
  }
  for (SemaphoreHandle_t sem : staged_) {  // every block of this frame is in the back frame
    xSemaphoreTake(sem, portMAX_DELAY);
    xSemaphoreGive(sem);
  }
  // The panel shows at most one frame per refresh: wait (asleep) until the last one is out
  xSemaphoreTake(idle_, portMAX_DELAY);
  for (; queued > 0; queued--) xSemaphoreTake(copied_, portMAX_DELAY);
  for (uint8_t& r : rows_) r = (r & kDrawn) ? kStale : 0;  // the new back frame lacks this frame's rows
  front_ = front ^ 1;
  xTaskNotifyGive(sender_);
}

// GDMA copy of panel rows [first, end) between the frames. Out of descriptors, it
// waits for the queued copies and tries again.
void Axs15231bQspiDisplay::copyRows(gfx::Color* dst, const gfx::Color* src, int first, int end, int& queued) {
  const size_t at = size_t(first) * cfg_.panelWidth;
  const size_t bytes = size_t(end - first) * cfg_.panelWidth * sizeof(gfx::Color);
  while (esp_async_memcpy(writer_, dst + at, const_cast<gfx::Color*>(src) + at, bytes, onCopied, copied_) != ESP_OK) {
    if (queued == 0) return;  // can never fit (logged by the driver): a stale strip, not a stuck UI
    for (; queued > 0; queued--) xSemaphoreTake(copied_, portMAX_DELAY);
  }
  queued++;
}

void Axs15231bQspiDisplay::waitIdle() {
  xSemaphoreTake(idle_, portMAX_DELAY);
  xSemaphoreGive(idle_);
}

void Axs15231bQspiDisplay::senderTask(void* self) {
  auto* d = static_cast<Axs15231bQspiDisplay*>(self);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (d->cfg_.te >= 0) {  // start as the next scan begins (a stale edge does not count)
      xSemaphoreTake(d->vsync_, 0);
      xSemaphoreTake(d->vsync_, pdMS_TO_TICKS(40));
    }
    d->sendFrame(d->frames_[d->front_]);
    xSemaphoreGive(d->idle_);
  }
}

bool IRAM_ATTR Axs15231bQspiDisplay::onCopied(async_memcpy_t, async_memcpy_event_t*, void* sem) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(sem), &woken);
  return woken == pdTRUE;
}

void IRAM_ATTR Axs15231bQspiDisplay::onVsync(void* sem) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(sem), &woken);
  if (woken) portYIELD_FROM_ISR();
}

void Axs15231bQspiDisplay::sendFrame(const gfx::Color* frame) {
  const int pw = cfg_.panelWidth, ph = cfg_.panelHeight;
  int next = 0, inFlight = 0;
  for (int r = 0; r < ph; r += cfg_.chunkRows) {
    const int n = std::min(cfg_.chunkRows, ph - r);
    const size_t bytes = size_t(n) * pw * sizeof(gfx::Color);
    if (inFlight == 2) {  // transfers finish in order: the oldest one used dma_[next]
      spi_transaction_t* done;
      spi_device_get_trans_result(dev_, &done, portMAX_DELAY);
      inFlight--;
    }
    gfx::Color* buf = dma_[next];
    esp_async_memcpy(reader_, buf, const_cast<gfx::Color*>(frame) + size_t(r) * pw, bytes, onCopied, read_);
    xSemaphoreTake(read_, portMAX_DELAY);
    spi_transaction_t& t = trans_[next];
    t = {};
    t.flags = SPI_TRANS_MODE_QIO;
    t.cmd = kOpWritePixels;
    t.addr = uint32_t(r == 0 ? kMemWrite : kMemWriteContinue) << 8;
    t.length = bytes * 8;
    t.tx_buffer = buf;
    spi_device_queue_trans(dev_, &t, portMAX_DELAY);
    inFlight++;
    next ^= 1;
  }
  for (; inFlight > 0; inFlight--) {
    spi_transaction_t* done;
    spi_device_get_trans_result(dev_, &done, portMAX_DELAY);
  }
}

void Axs15231bQspiDisplay::command(uint8_t cmd, const uint8_t* data, size_t len) {
  spi_transaction_t t = {};
  t.cmd = kOpWriteCmd;
  t.addr = uint32_t(cmd) << 8;
  t.length = len * 8;
  t.tx_buffer = len ? data : nullptr;
  spi_device_polling_transmit(dev_, &t);
}

}  // namespace drivers
