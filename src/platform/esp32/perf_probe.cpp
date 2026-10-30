#include "platform/esp32/perf_probe.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// after FreeRTOS.h, which must come first
#include "esp_freertos_hooks.h"
#include "esp_timer.h"

namespace perf {

namespace {
volatile uint32_t gIdleCount[2];
float gIdleRate[2];  // hook calls per microsecond on an idle core
uint32_t gLastCount[2];
int64_t gLastUs = 0;

// Returning false keeps the idle task looping, so the call count tracks idle time
bool idleHook0() {
  gIdleCount[0]++;
  return false;
}
bool idleHook1() {
  gIdleCount[1]++;
  return false;
}
}  // namespace

void begin(uint32_t calibrateMs) {
  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);

  uint32_t c0 = gIdleCount[0], c1 = gIdleCount[1];
  int64_t t0 = esp_timer_get_time();
  vTaskDelay(pdMS_TO_TICKS(calibrateMs));
  float dt = float(esp_timer_get_time() - t0);
  gIdleRate[0] = float(gIdleCount[0] - c0) / dt;
  gIdleRate[1] = float(gIdleCount[1] - c1) / dt;

  gLastCount[0] = gIdleCount[0];
  gLastCount[1] = gIdleCount[1];
  gLastUs = esp_timer_get_time();
}

void sample(float load[2]) {
  int64_t now = esp_timer_get_time();
  float dt = float(now - gLastUs);
  gLastUs = now;
  for (int i = 0; i < 2; i++) {
    uint32_t c = gIdleCount[i];
    float idle = gIdleRate[i] > 0 && dt > 0 ? float(c - gLastCount[i]) / dt / gIdleRate[i] : 1.0f;
    gLastCount[i] = c;
    float l = 100.0f * (1.0f - idle);
    load[i] = l < 0 ? 0 : (l > 100 ? 100 : l);
  }
}

}  // namespace perf
