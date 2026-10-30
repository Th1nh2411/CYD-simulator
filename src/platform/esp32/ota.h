#pragma once

#include <cstddef>
#include <cstdint>

// Over-the-air update: the firmware image comes from an https:// URL into the other app slot and the
// device restarts into it (esp_https_ota; the partition table has two slots). The server is checked
// against the CA kept by net::setPem() (own CA) or else the public ones, and the device shows its client
// certificate when it has one (the same material as the MQTT link). MQTT is stopped meanwhile, for the
// RAM of its TLS buffers, and comes back when the update fails. start() returns at once, the work is a
// task of its own.
//
// Nothing here checks who signed the image: whoever can make the device fetch a URL of a server it
// trusts can install firmware. Start it only from the serial port and the settings web server (LAN),
// not from MQTT, until images are signed (secure boot).
//
// A new image starts "pending verify" (rollback is on in the prebuilt bootloader): poll() marks it
// valid after kValidAfterMs of running; a reset before that, a crash for one, puts the old one back.
namespace platform::ota {

constexpr uint32_t kValidAfterMs = 30 * 1000;

// When this firmware was built (and -DBUILD_TAG="..." if given): tells two images apart
const char* build();

// False when the URL is not https:// or too long, or an update is already running
bool start(const char* url);

// {"state":"idle|running|done|failed","percent":N,"error":"...","minHeap":N,"build":"...","slot":"app0",
//  "image":"valid|pending|..."}
// percent is an estimate (this IDF cannot tell how much it has read); minHeap is the lowest internal
// free heap seen during the update
size_t toJson(char* out, size_t size);

// Loop task: marks the running image valid once it has run long enough
void poll(uint32_t nowMs);

}  // namespace platform::ota
