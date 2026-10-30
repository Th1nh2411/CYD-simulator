// Starter firmware for the ESP32 Cheap Yellow Display (ESP32-2432S028R).
// Verifies display, color order, touch, RGB LED and LDR.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "cyd_pins.h"

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Raw touch range for this panel; adjust if dots land off the finger
static const int TOUCH_X_MIN = 200, TOUCH_X_MAX = 3700;
static const int TOUCH_Y_MIN = 240, TOUCH_Y_MAX = 3800;

// Resistive touch gives noisy coordinates at light pressure (start/end of a tap),
// so require firm pressure, drop the first samples after press-down and
// reject samples that jump far from the previous one.
static const int TOUCH_Z_MIN = 600;           // library accepts >= 400
static const int TOUCH_SETTLE_SAMPLES = 2;
static const int TOUCH_MAX_JUMP = 150;        // raw units between consecutive samples

static const int STATUS_Y = 200;

static bool readTouch(TS_Point &raw, int &x, int &y) {
  static int pressedSamples = 0;
  static TS_Point prev;

  if (!(ts.tirqTouched() && ts.touched())) {
    pressedSamples = 0;
    return false;
  }
  TS_Point p = ts.getPoint();
  if (p.z < TOUCH_Z_MIN) {
    pressedSamples = 0;
    return false;
  }

  bool settled = ++pressedSamples > TOUCH_SETTLE_SAMPLES;
  bool steady = abs(p.x - prev.x) < TOUCH_MAX_JUMP && abs(p.y - prev.y) < TOUCH_MAX_JUMP;
  prev = p;
  if (!settled || !steady) return false;

  raw = p;
  x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, tft.width() - 1), 0, tft.width() - 1);
  y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, tft.height() - 1), 0, tft.height() - 1);
  return true;
}

// Prints display throughput and memory figures to serial (~1 s at boot)
static void runBenchmark() {
  const int frames = 10;
  uint32_t t0 = micros();
  for (int i = 0; i < frames; i++) tft.fillScreen(i & 1 ? TFT_NAVY : TFT_DARKGREEN);
  uint32_t fillUs = (micros() - t0) / frames;

  const int texts = 100;
  t0 = micros();
  for (int i = 0; i < texts; i++) tft.drawString("Benchmark 0123456789", 10, 10, 2);
  uint32_t textUs = (micros() - t0) / texts;

  Serial.println("--- benchmark ---");
  Serial.printf("CPU: %u MHz x %d cores\n", getCpuFrequencyMhz(), ESP.getChipCores());
  Serial.printf("LCD SPI clock: requested %u Hz, actual %u Hz\n", SPI_FREQUENCY,
                spiClockDivToFrequency(spiFrequencyToClockDiv(SPI_FREQUENCY)));
  Serial.printf("Full-screen fill 320x240: %.1f ms (%.1f fps)\n", fillUs / 1000.0, 1e6 / fillUs);
  Serial.printf("drawString (font 2, 20 chars): %u us\n", textUs);
  Serial.printf("Heap: free %u, largest block %u, total %u bytes\n", ESP.getFreeHeap(),
                ESP.getMaxAllocHeap(), ESP.getHeapSize());
  Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
  Serial.printf("App: %u bytes used of %u bytes partition\n", ESP.getSketchSize(),
                ESP.getFreeSketchSpace());
}

static void setLed(bool r, bool g, bool b) {
  digitalWrite(CYD_LED_RED, r ? LOW : HIGH);
  digitalWrite(CYD_LED_GREEN, g ? LOW : HIGH);
  digitalWrite(CYD_LED_BLUE, b ? LOW : HIGH);
}

static void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("CYD firmware ready", 10, 8, 4);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[64];
  snprintf(line, sizeof(line), "%s rev%d  %dMB flash", ESP.getChipModel(),
           ESP.getChipRevision(), ESP.getFlashChipSize() / (1024 * 1024));
  tft.drawString(line, 10, 40, 2);
  tft.drawString("build env: "
#ifdef ST7789_DRIVER
                 "cyd2usb"
#else
                 "cyd"
#endif
                 , 10, 58, 2);

  // Color bars: if a label does not match its bar color, the display config is wrong
  const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    int x = 10 + i * 76;
    tft.fillRect(x, 85, 70, 70, colors[i]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(names[i], x + 35, 160, 2);
  }

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Touch the screen...", 10, STATUS_Y, 2);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);
  setLed(false, false, false);

  Serial.println();
  Serial.println("=== CYD starter firmware ===");
  Serial.printf("Chip: %s rev%d, %d cores, flash %u bytes\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), ESP.getFlashChipSize());
  uint64_t mac = ESP.getEfuseMac();
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", (uint8_t)mac, (uint8_t)(mac >> 8),
                (uint8_t)(mac >> 16), (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  tft.init();
  tft.setRotation(1);  // landscape 320x240
  runBenchmark();
  drawScreen();

  // Boot LED sweep
  setLed(true, false, false);  delay(200);
  setLed(false, true, false);  delay(200);
  setLed(false, false, true);  delay(200);
  setLed(false, false, false);
}

void loop() {
  static uint32_t lastLdr = 0;

  TS_Point p;
  int x, y;
  if (readTouch(p, x, y)) {
    tft.fillCircle(x, y, 3, TFT_MAGENTA);
    char line[48];
    snprintf(line, sizeof(line), "Touch x=%3d y=%3d (raw %4d,%4d z=%4d)   ", x, y, p.x, p.y, p.z);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(line, 10, STATUS_Y, 2);
    Serial.println(line);
    setLed(false, true, false);
  } else {
    setLed(false, false, false);
  }

  if (millis() - lastLdr > 1000) {
    lastLdr = millis();
    char line[32];
    snprintf(line, sizeof(line), "LDR: %4d   ", analogRead(CYD_LDR));
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(line, 10, STATUS_Y + 20, 2);
  }

  delay(10);
}
