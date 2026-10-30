# Chạy local trên Mac

Các lệnh dùng hằng ngày cho device2 (simulator, Capri brain) và AI Buddy (`~/WorkSpace/ai_services`).
Mọi lệnh device2 chạy trong `~/WorkSpace/device2`.

## Cổng

| Cổng | Cái gì |
|---|---|
| 8766 | Simulator thiết bị (trình duyệt) |
| 8773 | Capri brain: chat AI, giọng nói, trang thử giọng |
| 11434 | Ollama (model AI chạy trên máy) |
| 1883 | RabbitMQ MQTT của AI Buddy (Colima/Docker) |
| 15672 | RabbitMQ trang quản lý |
| 3000 | AI Buddy backoffice API |
| 5173 | AI Buddy backoffice web: http://hanbiro.localhost:5173 |
| 8091 | Máy giả của AI Buddy (`simulator-real.js`) |

## Cài một lần

```bash
cd ~/WorkSpace/device2
brew install platformio                                   # build firmware (lệnh pio)
git clone https://github.com/emscripten-core/emsdk.git ../emsdk
(cd ../emsdk && EMSDK_PYTHON=/opt/homebrew/bin/python3 ./emsdk install latest && EMSDK_PYTHON=/opt/homebrew/bin/python3 ./emsdk activate latest)
/opt/homebrew/bin/python3 -m venv .venv                   # Python riêng của device2
.venv/bin/pip install pillow numpy opencv-python-headless sherpa-onnx
brew install ollama && brew services start ollama          # AI của Capri, chạy trên máy
ollama pull llama3.2                                       # model mặc định (đã có sẵn trên máy này)
```

Gói giọng của Capri (Kokoro, 316 MB, của lachydotmcg/capri-desktop), để ngoài repo:

```bash
mkdir -p ~/.capri_brain/voice-pack && cd ~/.capri_brain
curl -L -o capri-voice-1.zip https://github.com/lachydotmcg/capri-desktop/releases/download/voice-pack-1/capri-voice-1.zip
unzip -q capri-voice-1.zip -d voice-pack && rm capri-voice-1.zip
```

Tài khoản MQTT nằm ở `~/.capri_brain/config.json` (quyền 600, không bao giờ đưa vào repo; giọng đã chọn ở trang brain cũng lưu vào đây):

```json
{"mqtt_host": "127.0.0.1", "mqtt_user": "aibuddy", "mqtt_pass": "…"}
```

`mqtt_pass` là `RABBITMQ_DEFAULT_PASS` của container: `docker inspect ai-buddy-dev-rabbitmq-1 | grep RABBITMQ_DEFAULT_PASS`.

## 1. Chỉ simulator + Capri (không cần AI Buddy)

```bash
# Terminal 1: Capri brain (chat AI + giọng). Không chạy cái này thì vẫn có tiếng chào và blip
.venv/bin/python tools/capri/capri_brain.py serve --no-mqtt --http 8773
open http://localhost:8773                       # thử giọng, so với hello-*.wav, "Use for Capri"

# Terminal 2: simulator
.venv/bin/python tools/web/sim.py
open "http://localhost:8766/?board=jc3248w535"   # màn 480x320 của JC; không ?board = 320x240
```

Trên trang: bấm **🔊 Sound on** (trình duyệt chỉ phát tiếng sau một cú bấm), chọn **Capri**, bấm **TALK** để chat.
Trang luôn mở bằng slime.

Một terminal duy nhất: chạy brain ở nền

```bash
.venv/bin/python tools/capri/capri_brain.py serve --no-mqtt --http 8773 > /tmp/capri-brain.log 2>&1 &
.venv/bin/python tools/web/sim.py
```

## 2. Hệ thống AI Buddy (backoffice + MQ engine)

```bash
colima start                                     # nếu Docker chưa chạy
cd ~/WorkSpace/ai_services
pnpm infra:up                                    # RabbitMQ + PostgreSQL
pnpm --filter @ai-buddy/contracts build
pnpm --filter @ai-buddy/backoffice-server build
pnpm --filter @ai-buddy/mq-server build
pnpm --filter @ai-buddy/mq-server db:migrate     # lần đầu, hoặc khi có migration mới

# mỗi cái một terminal
cd ~/WorkSpace/ai_services/apps/backoffice-server && pnpm start
cd ~/WorkSpace/ai_services/apps/mq-server && pnpm start
cd ~/WorkSpace/ai_services/apps/backoffice-web && pnpm dev
```

Web: http://hanbiro.localhost:5173, đăng nhập `ceo@hanbiro.example` / `password`.
HB-0006 đã gán cho Rin (`rinktvn2525@hanbiro.example`); OTP của nó ở mục thiết bị.

## 3. Simulator làm thiết bị AI Buddy HB-0006

Cần phần 2 đang chạy.

```bash
cd ~/WorkSpace/device2
.venv/bin/python tools/web/sim.py --aibuddy HB-0006          # --wifi HANBIRO-5G mặc định
open "http://localhost:8766/?board=jc3248w535"
```

Màn hình hiện bàn phím OTP → nhập OTP của HB-0006 từ web. Lần sau tự đăng nhập (session lưu trong trình duyệt).
Tạo task giao cho Rin, bật thông báo thiết bị → thẻ hiện trên simulator → bấm Done → task thành done.

## 4. Máy thật (JC3248W535)

```bash
cd ~/WorkSpace/device2
pio run -e jc3248w535 -t upload
pio device monitor -b 115200                     # xem log serial (Ctrl+C để thoát)
```

Gõ vào serial (hoặc trang cài đặt của máy, mục 네트워크):

```
net {"wifi":{"ssid":"<Wi-Fi>","pass":"…"},"mqtt":{"host":"<IP của Mac>","port":1883},"aibuddy":{"device":"HB-0006","secret":"HB-0006_secret_dev"}}
otp 123456
char capri
```

IP của Mac: `ipconfig getifaddr en0`. Wi-Fi phải có trong danh sách cho phép của HB-0006 (sửa trên web nếu bị `wifi_not_allowed`).

Brain với máy thật ở chế độ thử `blob/...` (không phải AI Buddy):

```bash
.venv/bin/python tools/capri/capri_brain.py serve --topic blob/<4 ký tự cuối MAC>
```

## Tắt

```bash
pkill -f capri_brain.py
pkill -f tools/web/sim.py
pkill -f "dist/main.js"                          # backoffice-server và mq-server
cd ~/WorkSpace/ai_services && pnpm infra:down    # RabbitMQ + PostgreSQL
```

## Khi có lỗi

| Lỗi | Cách xử lý |
|---|---|
| `Address already in use` | Đã có một bản đang chạy: `pkill -f capri_brain.py` hoặc `pkill -f tools/web/sim.py`, hoặc đổi cổng (`--http 8774`, `--port 8767`) |
| `No module named 'cv2'` / `'sherpa_onnx'` | Chạy bằng `.venv/bin/python`, không phải `python3` |
| Không có tiếng | Bấm 🔊 Sound on hoặc bấm vào trang một lần |
| Capri không trả lời TALK | Brain chưa chạy ở 8773, hoặc Ollama chưa chạy: `brew services start ollama` (log brain ghi `Ollama not reachable`) |
| Lần TALK đầu báo offline | Model đang nạp; brain nạp sẵn khi khởi động, đợi dòng `llama3.2 loaded` |
| Capri trả lời tiếng Anh khi hỏi tiếng Hàn | Capri trả lời theo ngôn ngữ người hỏi (Hàn / Việt / Anh); `llama3.2` tiếng Hàn yếu nên khi hai lần không ra tiếng Hàn, brain dùng câu tiếng Anh thay vì báo mất sóng |
| Tiếng Hàn của Capri gượng | `llama3.2` nhỏ, tiếng Hàn yếu: `ollama pull qwen2.5:3b` rồi chạy brain với `--model qwen2.5:3b`, hoặc chọn English cho Capri |
| `broker refused the connection (code 4)` | Thiếu/sai `mqtt_user`, `mqtt_pass` trong `config.json` |
