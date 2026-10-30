#include "platform/esp32/ota.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "platform/esp32/network.h"
#include "sdkconfig.h"

#ifndef OTA_TEST_CRASH
#define OTA_TEST_CRASH 0
#endif
#ifndef BUILD_TAG
#define BUILD_TAG ""
#endif

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace platform::ota {

namespace {

enum class State : uint8_t { Idle, Running, Done, Failed };
const char* const kStateNames[] = {"idle", "running", "done", "failed"};

constexpr size_t kMaxUrl = 255;
constexpr size_t kChunk = 4096;               // read from the socket and written to flash in these
constexpr int64_t kSlowUs = 1000 * 1000;      // a read or a flash write that takes longer is reported

char gUrl[kMaxUrl + 1];
char gError[48] = "";
std::atomic<State> gState{State::Idle};
std::atomic<int> gPercent{0};
std::atomic<uint32_t> gMinHeap{0};

uint32_t freeInternal() { return uint32_t(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)); }

void fail(const char* why) {
  snprintf(gError, sizeof(gError), "%s", why);
  gState = State::Failed;
  Serial.printf("ota: failed: %s (lowest internal heap %u)\n", gError, unsigned(gMinHeap.load()));
  net::setMqttPaused(false);
}

// Reads the image from the open connection into the other app slot and makes that slot the one to
// boot. HTTP and flash are driven by hand (not esp_https_ota) for the exact progress and to tell a
// slow read from a slow flash write.
esp_err_t download(const esp_http_client_config_t& http) {
  esp_http_client_handle_t client = esp_http_client_init(&http);
  if (!client) return ESP_ERR_NO_MEM;
  esp_err_t err = esp_http_client_open(client, 0);  // the TLS handshake is here
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }
  const int64_t total = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  if (status != 200 || total <= 0 || !slot || total > int64_t(slot->size)) {
    Serial.printf("ota: the server answered %d with %lld bytes, the slot holds %u\n", status, (long long)total,
                  slot ? unsigned(slot->size) : 0u);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status != 200 ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_INVALID_SIZE;
  }
  Serial.printf("ota: %lld bytes into %s\n", (long long)total, slot->label);

  esp_ota_handle_t ota = 0;
  err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota);
  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));
  if (err == ESP_OK && !buf) err = ESP_ERR_NO_MEM;
  int64_t got = 0;
  int shown = -1;
  while (err == ESP_OK && got < total) {
    const int64_t t0 = esp_timer_get_time();
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(buf), kChunk);
    const int64_t t1 = esp_timer_get_time();
    if (n == -ESP_ERR_HTTP_EAGAIN) err = ESP_ERR_TIMEOUT;  // nothing came for timeout_ms
    else if (n < 0) err = ESP_FAIL;
    else if (n == 0) err = esp_http_client_is_complete_data_received(client) ? ESP_OK : ESP_ERR_TIMEOUT;
    if (err != ESP_OK || n == 0) break;
    err = esp_ota_write(ota, buf, size_t(n));  // the first chunk is checked to be an image of this chip
    const int64_t t2 = esp_timer_get_time();
    got += n;
    if (t1 - t0 > kSlowUs) Serial.printf("ota: a read took %lld ms at %lld bytes\n", (long long)((t1 - t0) / 1000), (long long)got);
    if (t2 - t1 > kSlowUs) Serial.printf("ota: a flash write took %lld ms at %lld bytes\n", (long long)((t2 - t1) / 1000), (long long)got);
    gMinHeap = std::min(gMinHeap.load(), freeInternal());
    const int percent = int(got * 100 / total);
    gPercent = std::min(percent, 99);
    if (percent / 10 != shown) {
      shown = percent / 10;
      Serial.printf("ota: %d%%\n", percent);
    }
  }
  free(buf);
  if (err == ESP_OK && got != total) err = ESP_ERR_INVALID_SIZE;  // the server stopped before the whole image came
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK) {
    if (ota) esp_ota_abort(ota);
    return err;
  }
  err = esp_ota_end(ota);  // checks the image: ESP_ERR_OTA_VALIDATE_FAILED when it is not a good one
  if (err == ESP_OK) err = esp_ota_set_boot_partition(slot);
  esp_app_desc_t desc;
  if (err == ESP_OK && esp_ota_get_partition_description(slot, &desc) == ESP_OK) {
    Serial.printf("ota: image %s %s, built %s %s\n", desc.project_name, desc.version, desc.date, desc.time);
  }
  return err;
}

void updateTask(void*) {
  gMinHeap = freeInternal();
  net::setMqttPaused(true);
  for (int i = 0; i < 40 && !net::mqttDown(); i++) vTaskDelay(pdMS_TO_TICKS(100));
  gMinHeap = std::min(gMinHeap.load(), freeInternal());

  char* ca = net::copyPem(net::Pem::Ca);
  char* cert = net::copyPem(net::Pem::ClientCert);
  char* key = net::copyPem(net::Pem::ClientKey);
  esp_http_client_config_t http = {};
  http.url = gUrl;
  http.timeout_ms = 15000;
  if (ca) http.cert_pem = ca;  // own CA alone
  else http.crt_bundle_attach = esp_crt_bundle_attach;  // public CAs
  if (cert && key) {
    http.client_cert_pem = cert;
    http.client_key_pem = key;
  }
  Serial.printf("ota: %s%s%s\n", gUrl, ca ? " (own CA)" : " (public CAs)", cert && key ? ", client certificate" : "");
  const esp_err_t err = download(http);
  free(ca);
  free(cert);
  free(key);

  if (err != ESP_OK) {
    fail(esp_err_to_name(err));
  } else {
    gPercent = 100;
    gState = State::Done;
    Serial.printf("ota: done, restarting into the new image (lowest internal heap %u)\n", unsigned(gMinHeap.load()));
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }
  vTaskDelete(nullptr);
}

}  // namespace

const char* build() { return __DATE__ " " __TIME__ " " BUILD_TAG; }

bool start(const char* url) {
  if (!url || strncmp(url, "https://", 8) != 0 || strlen(url) > kMaxUrl) return false;
  if (gState == State::Running) return false;
  snprintf(gUrl, sizeof(gUrl), "%s", url);
  gError[0] = '\0';
  gPercent = 0;
  gState = State::Running;
  // The loop task has 8 KB; TLS and the HTTP client want their stack, so the task gets its own
  if (xTaskCreatePinnedToCore(updateTask, "ota", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    fail("no memory for the task");
    return false;
  }
  return true;
}

size_t toJson(char* out, size_t size) {
  JsonDocument doc;
  doc["state"] = kStateNames[int(gState.load())];
  doc["percent"] = gPercent.load();
  doc["error"] = gError;
  doc["minHeap"] = gMinHeap.load();
  doc["build"] = build();
  const esp_partition_t* running = esp_ota_get_running_partition();
  doc["slot"] = running ? running->label : "?";
  // "pending" is a new image that has not run kValidAfterMs yet: a reset now puts the old one back
  static const char* const kImageNames[] = {"new", "pending", "valid", "invalid", "aborted"};
  esp_ota_img_states_t image;
  if (running && esp_ota_get_state_partition(running, &image) == ESP_OK && int(image) >= 0 && int(image) < 5) {
    doc["image"] = kImageNames[int(image)];
  } else {
    doc["image"] = "valid";  // the first USB flash has no state, nothing to roll back to
  }
  size_t n = serializeJson(doc, out, size);
  return n < size ? n : 0;
}

void poll(uint32_t nowMs) {
#if OTA_TEST_CRASH
  // Test of the rollback: this image dies a few seconds after it starts
  if (nowMs > 8000) abort();
#endif
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  static bool checked = false;
  if (checked || nowMs < kValidAfterMs) return;
  checked = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("ota: the new firmware ran long enough, marked valid");
  }
#else
  (void)nowMs;
#endif
}

}  // namespace platform::ota

#ifdef CONFIG_APP_ROLLBACK_ENABLE
// The Arduino core marks a new image valid before setup() unless this says later: poll() does it
// after kValidAfterMs, so an image that crashes in its first seconds is rolled back
extern "C" bool verifyRollbackLater() { return true; }
#endif
