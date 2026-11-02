# 인수인계: CYD 캐릭터 펌웨어

다른 세션에서 이어서 작업할 때 읽는 문서입니다. 빌드 명령과 작업 규칙은 [CLAUDE.md](../CLAUDE.md), 사용자용 설명은 [README.md](../README.md)에 있습니다.

## 1. 현재 상태 (2026-10-30)

- **brain = Ollama만 (2026-10-30, 사용자 요청 "Gemini 빼고 Ollama, 있는 걸로"):** Gemini 무료 키가 분당 한도(429)에 걸려 매번 "[SIGNAL CORRUPTED]" 오프라인 카드가 나왔음 → Gemini(LLM·TTS·키·옵션) 전부 삭제, `~/.capri_brain/config.json`의 `gemini_key`도 지움. 기본 모델 `llama3.2`(이 Mac에 원래 있던 것, 3B), 시작할 때 미리 로드(첫 답 37초 → 12~16초, 기기 대기 20초 안). 작은 모델 대책: 글리치 태그 문장을 페르소나에서 빼고 답에서 `[...]` 제거, 기기 글꼴에 없는 글자(태국어·베트남어 등) 제거, 리마인더는 사용자가 "알려/remind"라고 했을 때만, 답 버튼은 단어 경계에서 자름. 확인: talk·chat·checkin 응답. **한국어 품질은 낮음**(어색한 문장, 영어 섞임): 더 나은 한국어는 `qwen2.5:3b` 등을 받아 `--model`. 목소리는 Kokoro만.
- **묻는 언어로 답 + 베트남어 글꼴 (2026-10-30, 사용자 "묻는 사람 언어로 답해도 되지만 글자 깨지지 않게"):** `kFontKo11`에 베트남어 134자 추가(갈무리11에 있음, `tools/font/bdf2font.py` `VIETNAMESE`). 줄 높이 15 px·기준선 12는 한글 그대로(한글 화면 불변), 겹친 성조 기호는 줄 위로 최대 5 px → `Font::above`, `Canvas::drawText`의 띠 건너뛰기 조건에 반영. ASCII만인 카드는 여전히 5×7(slime 불변). brain: `spoken()`이 한글→ko, 베트남어 글자→vi, 영어 두 단어 이상→en을 고르고 답 버튼 탭은 그 언어를 이어감(`pending`). 다른 언어 답이면 한 번 더(두 번째는 대화 기록 없이), 그래도 안 되면 기기가 보여줄 수 있는 영어 답을 씀(오프라인 카드 대신). 글꼴에 없는 글자가 20% 넘으면 거부, 몇 글자는 `clean()`이 지움. 확인: 시뮬레이터 스크린숏(베트남어 대문자 Ấ·Ổ 포함), jc3248w535·cyd2usb 빌드, llama3.2로 ko/vi/en 질문. 테스트로 섞인 `~/.capri_brain/state.json` 대화 기록은 비움(`state.json.bak`).

- **Capri 등장 인트로 (2026-10-30, 사용자 요청 "처음 열 때/바꿀 때 인트로"):** 활성이 될 때마다(부팅, `char capri`) capri-desktop의 인트로를 그 박자(24 fps, fx.js `INTRO_T`)대로: 좌우 가장자리가 빨갛게 지직(250 ms) → 손이 가장자리를 잡음(그림의 52~72% 행만, 좌·우 덜컥) → 끌어올리며 전체가 드러남 → 유리에 부딪힘(흔들림 + 잔상) → "hi"(입 셀 `SAY_HI`) + 시간대 인사 음성·대사 → 팔을 떼고 손 흔들기 → 다시 잡고 웃는 눈 → 3.36초에 평소 자세. capri-desktop 끝의 줄어들어 떨어지는 부분은 뺌(기기에서는 늘 클로즈업). `FigureLayer::Pose::rowsFrom/rowsTo`(보이는 행), `VoidLayer::setEdges`. 시뮬레이터에서 0.15~3.6초 프레임으로 확인.
- **Capri 목소리·활동·대화 버튼, 설정 페이지 영어 (2026-10-30, 사용자 요청):**
  - **그녀 자신의 목소리(2026-10-30, 사용자 요청 "voice 폴더 wav 같은 목소리"):** brain의 기본 목소리 = lachydotmcg/capri-desktop의 음성 팩 `capri-voice-1`(Kokoro-82M, Apache-2.0, 316 MB, `~/.capri_brain/voice-pack`에 받아 둠, 저장소 밖) speaker 53 "Her own", en-us. **기본값은 capri-desktop 0.7.10의 튜닝 목소리(2026-11-02, 사용자 요청): 속도 1.32(스타일 1.1 × 120%), +2 반음.** 이전 기본값은 속도 1.1, +6 반음이었음(`--pitch`; 인사 wav는 중앙값 약 360 Hz, 이 목소리는 약 200 Hz라 54개 speaker 어느 것도 그대로는 안 맞았음: 인사 클립은 웃음이 섞였거나 올려 둔 것으로 보임). 같은 속도로 올리기는 capri-desktop의 pitch 슬라이더와 같은 방법(Kokoro를 비율만큼 느리게 + 리샘플). 한 줄 약 2.5~3.5초, PC에서 무료. **brain은 `.venv/bin/python`으로 실행**(sherpa-onnx). 팩이 없거나 `--voice-engine gemini`면 Gemini 음성. 사전에 없는 말(예: "Heh")은 건너뜀. 소리는 Claude가 들어 보지 못함: 사용자 확인 필요.
  - **목소리:** 그녀가 하는 말은 모두 영어로 소리 내어 말함(화면 글은 설정 언어 그대로, 자막처럼). 정해진 대사는 같은 순서의 영어 대사(`LinePicker::pick`의 english), brain 대화는 모델이 주는 `speech`(영어). 캐릭터 outbox `takeSpeech` → `{"text"}` 또는 `{"ref"}`(brain 카드). brain `--http`의 `POST /tts`가 Gemini 음성 모델(`--tts-model gemini-3.8-flash-lite-tts`, `--voice Leda`, 한 줄 약 3~5초)로 WAV를 만들고 `~/.capri_brain/tts/`에 저장(같은 줄은 한 번만). 인사는 원래 녹음(hello-*.wav). 목소리가 켜져 있으면 글자 블립은 안 냄(설정 `voice`). **시뮬레이터만**: 기기(ESP32)는 아직 `takeSpeech`를 쓰지 않음(브레인 HTTP에서 WAV를 받아 I2S로 흘리는 일이 남음).
  - **대화가 보이게:** 오른쪽 아래 `TALK` 칩(누르면 길게 누르기와 같음, brain을 기다리는 동안 켜짐). 먼저 말 걸기 기본 30 → 5분, brain에게 `{"type":"checkin"}`을 묻고 답이 없으면 정해진 대사. 9~21초마다 혼자 작은 동작(손 흔들기, 웃는 눈, 중얼거림, 폴짝, 글리치).
  - **설정 페이지:** 머리의 언어 고르기(한국어 / English, localStorage `blob.lang`). 한국어 원문 → 영어 표(`I18N`), `MutationObserver`가 스크립트가 그리는 글까지 바꿈(바뀐 때만 써서 자기 자신을 다시 부르지 않게: 처음엔 placeholder를 매번 다시 써서 페이지가 멈췄음).
  - brain: 모델이 실패해도 "N분 뒤 알려줘" 리마인더는 정규식으로 저장. 모든 chat 카드에 ref.
  - **확인:** 시뮬레이터에서 TALK → "Hold on, thinking…" 음성 → Gemini 답(한국어 + 답 버튼 3개) → 영어 음성 WAV 생성(소리는 들어 보지 못함), 설정 페이지 영어(남은 한글은 "한국어" 선택지뿐), jc3248w535 빌드.

- **브라우저 시뮬레이터 = AI Buddy 기기 (2026-10-30, 사용자 요청 "기기가 없으니 브라우저에서"):** `python tools/web/sim.py --aibuddy HB-0006 [--aibuddy-secret …] [--mqtt-host 127.0.0.1] [--wifi HANBIRO-5G]`. 프로토콜은 `src/app/net/aibuddy.*`(이식 가능, ESP32와 같은 코드; `platform/esp32/aibuddy.cpp`는 NVS·SSID 해시·RNG·뮤텍스만), 브라우저는 TCP를 못 열어서 sim.py가 MQTT 연결(client id = user = 번호, last will 포함: `blob_mqtt.Client`에 `client_id`, `will` 추가)을 들고 SSE(`/__aibuddy/events`)와 POST(`/__aibuddy/up`)로 페이지와 프레임을 주고받음. 세션 토큰은 localStorage `cyd.aibsess.<번호>`. **확인:** 시뮬레이터에서 키패드에 백오피스 OTP → `auth` OK(RIn) → Rin에게 만든 task가 Capri 카드(Done/Later/Seen)로 → Done 탭 → 백오피스 task `done`. 시뮬레이터 빌드에는 Homebrew Python에 pillow·numpy·opencv가 필요(사용자가 지우게 해서 이번엔 scratchpad의 임시 venv로 돌림).
- **Capri 그림 교체 (2026-10-30, 사용자 요청 "capri-cutout·capri-full 말고 intro-*"):** 몸 = 인트로 클로즈업 셀 9장(ARCHITECTURE 8d). 순간이동은 없앰(그녀가 화면 폭을 다 씀), 패널은 늘 아래. 플래시 JC 3.47 MB(52.9%).

- **AI Buddy 기기 프로토콜 (2026-10-30, 사용자 요청: "AI Buddy의 RabbitMQ를 그대로 쓰고 hanbiro의 Rin에게 기기 하나 연결"):** 이 저장소와 `~/WorkSpace/ai_services`(AI Buddy: 백오피스 + MQ 서버)는 한 제품이고 이 펌웨어가 그 기기. 규격은 `ai_services/docs/design/mq-server/en/device-connection.md`(시험 단계 JSON 프레임), 참조 구현은 `ai_services/simulator/simulator-real.js`.
  - **펌웨어:** `platform/esp32/aibuddy.*`(프레임 읽고 쓰기), `network.cpp`는 `"aibuddy":{"device","secret"}`가 있으면 그 모드: client id = user = 번호, password = 비밀키, `d/<번호>/dn` 구독, `/st`에 `{"isConnected":true}`(last will `false`, retain 없음, clean session), `blob/...` 토픽·outcome·배터리·ask는 안 보냄(기기 계정은 자기 토픽만 허용). 로그인: 저장된 세션 토큰(NVS `aibsess`, 번호는 `aibdev`) 또는 OTP. OTP는 화면 키패드(`app/ui/otp_screen`, 실패 코드마다 한국어 안내), 시리얼 `otp 123456`, 웹 `POST /api/otp`, 설정 페이지 "네트워크"의 OTP 칸. `noti` → sticky 승인 요청 카드(task: 완료/나중에/확인 → `done/later/seen`, 나머지: 확인/나중에), 제목은 종류(할 일/일정/공지, 언어는 auth의 `language`가 ko면 한국어), 본문 + 마감/시작 시각(로컬), Capri 태그(urgent·high = alert, task = work, schedule = remind, notice = info, isRejected = error). 누르면 `ack`(UUID v4, `sourceMessageId`), `ackok`까지 3초마다 다시 보냄(최대 8개, 재로그인 때도). `summary` → 알림 카드, `logout`/`auth` 실패 → OTP 화면 또는 오류 카드, `reauth` → 다시 로그인, `cancel` → 그 카드를 눌러도 아무것도 안 보냄(화면에서 바로 지우지는 못함). `deviceInfo`: `FIRMWARE_VERSION`(기본 `0.9.0`, 계약이 semver를 요구), maxFrameBytes 2048, listCapacity 6(엔진 카드 큐), 언어 en·ko. `wifiHash` = 지금 SSID의 SHA-256.
  - **한 일(로컬 AI Buddy):** `ai_services`에서 backoffice-server(3000)와 mq-server를 빌드·실행(그대로 떠 있음, 로그 `/tmp/claude-501/aib_bo.log`·`aib_mq.log`), 백오피스 API로 **HB-0006을 hanbiro의 Rin(`rinktvn2525@hanbiro.example`)에게 배정**(허용 Wi-Fi HANBIRO-5G/2G, Office-5G), 시험 task를 위해 Rin을 project "AI Buddy device"에 member로 추가하고 task "[테스트] Capri 연결 확인"을 만듦(완료 처리됨).
  - **확인한 것:** 펌웨어가 보낼 JSON 그대로(Python 가짜 기기)로 진짜 엔진에서 OTP 로그인 → `auth` OK(RIn) → task `noti` → Done `ack` → `ackok` → 백오피스에서 task `done`. 펌웨어 빌드(jc3248w535 정적 RAM 109.6 KB, +5.5 KB 대부분 미확인 응답 표), muma·s3lcd13·cyd2usb 빌드, 시뮬레이터에서 OTP 키패드(480×320, 240×240; 시뮬레이터는 `POST /api/otp`를 code 없이 보내면 키패드가 열림).
  - **확인 못 한 것:** 펌웨어 자체가 엔진과 주고받는 것(기기에 못 올림). 기기에서 할 일: `net {"wifi":{"ssid":"…","pass":"…"},"mqtt":{"host":"<Mac IP>","port":1883,"tls":false},"aibuddy":{"device":"HB-0006","secret":"HB-0006_secret_dev"}}` → 화면 키패드에 백오피스(HB-0006 상세의 OTP)나 `GET /api/v1/devices/HB-0006/otp`의 6자리. 허용 Wi-Fi에 그 기기의 SSID가 없으면 `wifi_not_allowed`(백오피스에서 HB-0006 Wi-Fi 설정에 추가). Mac의 1883은 Colima가 모든 인터페이스로 열고 있음(macOS 방화벽은 확인 안 함).
  - **AI Buddy 모드에서 Capri:** 카드는 그녀의 말투로 나옴. brain 대화(`blob/<id>/ask`)는 기기 계정으로는 못 보내서 길게 누르면 20초 뒤 "뇌가 오프라인". 다음 후보: 기기가 brain의 HTTP 포트(`--http`)로 직접 묻기.
  - **남은 규격 일(device-connection.md §4):** 시간대(IANA → POSIX, 지금은 기기 `tz` 분), 언어팩 확인, `style` 프레임(화면 모드), 같은 키 덮어쓰기·`cancel`을 화면에 반영(엔진 큐에 카드 지우기가 없음), 운영 빌드에서 MQTT 설정 잠금과 비밀값 암호화, TLS 8883.

- **네 번째 캐릭터 Capri (`capri`, 2026-10-30, 사용자 요청: "capri-desktop 에셋 + capri-desktop2 목소리, AI 기능, Capri식 알림"):** 설계·파일은 ARCHITECTURE 8d, 출처·권리는 `art/capri/CREDITS.md`.
  - **출처:** 그림 = mochiiwuff/capri-desktop(`~/WorkSpace/capri-desktop`, 전신 누끼·얼굴·컷인. **애니 장면 캡처와 팬 누끼라 코드 라이선스 밖, 권리자 소유**), 목소리 = lachydotmcg/capri-desktop(`~/WorkSpace/capri-desktop2`: 성격 프롬프트, 대사, 인사 음성 3개). 둘 다 PolyForm Noncommercial → **비상업만**, Required Notice를 같이 배포.
  - **기기:** 밤하늘 + 둥실·갈기·글리치·잔상, 가로 화면은 패널 자리로 순간이동. 카드는 cyberpunk 패널에 그녀의 첫 줄 + 본문, 태그(`tag`: info alert done error work remind idle start end chat)마다 배지·색·얼굴·컷인·소리. 혼자: 시간대 인사(음성), 쓰다듬기/찌르기, 길게 누르면 brain에게 말 걸기, 먼저 말 걸기, 2시간 방치 시 졸라댐, 졸기. 한국어/영어.
  - **엔진 변경:** `CardContent`에 `tag[9]`, `face[9]`(카드 JSON `"tag"`, `"face"`; 다른 캐릭터는 무시). `Character::takeAsk/takeSound`(기본 없음), `View::takeAsk/takeSound`.
  - **플랫폼:** MQTT `<topic>/ask`(보냄), 웹 `POST /api/ask`(설정 페이지 "Capri에게 말 걸기"), 시리얼 `ask <text>`, `hal::Speaker` + `drivers/esp32/i2s_speaker`(JC만, NS4168 BCLK 42 / LRCLK 2 / DIN 41), 오디오 태스크(코어0). `max_uri_handlers` 20 → 22. `WITH_CAPRI=0`: CYD 두 env와 재생 env 세 개(태그와 같은 캐릭터 목록).
  - **brain(PC):** `python tools/capri/capri_brain.py serve [--model gemini-3.5-flash] [--http 8773]`, Gemini API만(키는 `$GEMINI_API_KEY` 또는 `~/.capri_brain/gemini_key`(저장소 밖, 권한 600; 사용자 키를 이 Mac에 넣어 둠); 표준 라이브러리만; 사용자 요청으로 Anthropic SDK와 Ollama는 뺌, 2026-10-30). 키가 없거나 API가 실패하면 대화만 "뇌가 오프라인" 카드, 리마인더(`remind`)와 훅은 그대로. Claude Code 훅: `print-hooks`가 `~/.claude/settings.json`에 넣을 블록을 출력(권한 요청은 기기에서 허용/거절).
  - **확인한 것(Mac, 2026-10-30):** `pio run -e jc3248w535` 성공(정적 RAM 104.1 KB, 플래시 2.49 MB), muma·s3lcd13·replay_s3·cyd2usb 성공. **cyd2usb_anime는 100.9%로 넘침**(이번 추가분은 Synth 등 약 5 KB, 나머지는 10-26 이후 기능; CYD는 안 씀). 시뮬레이터(480×320, 240×240)에서 인사·순간이동·컷인·done/error/remind/prompt·반응·찌르기·길게 누르기 → brain 대화 화면 스크린샷. brain은 시험 브로커 + 가짜 기기 + 가짜 LLM으로 talk → 답 버튼 → 다음 턴, 리마인더(완료/미루기), HTTP 포트, 훅(권한 거절 → `deny` 출력, Stop 카드) 확인.
  - **Gemini 실제 호출(2026-10-30, 사용자 키):** `gemini-3.5-flash`로 talk → 답 버튼 → 다음 턴, 한국어 리마인더("5분 뒤에 … 알려줘" → `remind_in_min` 5), 영어 모두 됨. 답 하나에 7~9초라 기기 대기 `kBrainWaitMs`를 12 → 20초로. `gemini-2.5-*`는 새 사용자에게 404("no longer available to new users"). 브로커가 로그인을 요구하면 `--user`/`--pass`(또는 `$CAPRI_MQTT_USER`/`$CAPRI_MQTT_PASS`), `tools/mqtt/blob_mqtt.py`의 `Client`도 로그인 지원.
  - **확인 못 한 것:** 기기에 올려 보기(이 Mac에서는 보드 없음), **스피커 소리(NS4168 핀은 회로도 기준, 스피커가 달렸는지 모름)**, 힙·CPU(팔레트 2벌로 정적 RAM +7.4 KB, 오디오 태스크 스택 3 KB), 실제 Claude Code에서 훅, 슬라임 재생 테스트(기기 필요; `CardContent` 크기가 바뀌었지만 그리는 코드는 그대로).
  - **이 Mac 환경:** `brew install platformio`(pio 6.2), emsdk는 `~/WorkSpace/emsdk`(Homebrew Python 3.15로 설치, 시뮬레이터도 `/opt/homebrew/bin/python3 tools/web/sim.py`; 그 Python의 pillow·numpy는 사용자 요청으로 지움, 시뮬레이터를 다시 쓰려면 설치 필요), 그림 도구는 `/usr/bin/python3`(Pillow·numpy 있음). 원격 저장소·git 없음(`device2`는 git 저장소가 아님).

- **배터리 읽기·MQTT, NTP, TLS 버퍼를 PSRAM으로 (2026-10-29, 브랜치 `heap-profile`, JC3248W535에서 시험, 아직 `main`에 안 합침):**
  - **JC의 배터리:** 충전·승압은 IP5306(소프트웨어가 건드릴 것 없음, **충전 중인지 알려주는 핀·충전 LED 없음**). SW1(배터리 커넥터 옆 작은 버튼)은 USB 없이 배터리로 켤 때 누르는 IP5306 KEY 스위치. 배터리 전압은 IO5의 분압기: **회로도는 R26 33k지만 이 기기는 R26이 `683`(68k), R27 100k(`01D`)라 ×1.68**(`board.batteryScale`). 배터리를 뽑았을 때와 충전이 평평해지는 값이 같은 4.15 V라 충전 종료 4.2 V면 ×1.70, 즉 1.2% 안쪽. **멀티미터로는 못 재 봄.** 이 기기 말고 다른 JC는 부품이 다를 수 있음.
  - 시리얼 `batt`(mV), `light 0~255`(백라이트, 부팅 때 한 번만 켜므로 그대로 유지). MQTT `<topic>/battery`(retained `{"mv":N}`, 5분마다, MQTT가 안 붙어 있으면 5초마다 재시도)와 `<topic>/battery/get`(받으면 바로 답함). 다른 보드는 `batteryPin = -1`이라 보내지 않음(빌드는 안 해 봄).
  - **PC 페이지 `tools/mqtt/battery_web.py`**(`http://localhost:8772`, 브로커에 클라이언트로 붙음): 전압·%·원값 옆 중앙값/이동평균(최근 5개)·그래프·"지금 읽기". %는 일반 리튬폴리머 곡선이라 **이 배터리로 보정 안 됨**. `--load history.json`으로 이전 기록을 이어 받음.
  - **측정(850 mAh 3.7 V, 화면 켠 채):** 방전은 부하 따라 분당 4~9 mV(프리셋 순환 약 8.7, 알림 약 4)로 3.4 V대까지 갔고 **끝까지(꺼질 때까지)는 못 감**, 0%는 일반값(3.3 V대). 충전: 3.43 → 4.14 V에 약 1시간 반, **화면을 끄면 한 번에 +42 mV, 속도 약 1.7배**, 끝나고 쉬는 전압 약 4.07 V. USB를 뽑으면 바로 약 70 mV 내려감(충전 중 전압이 높게 읽힘). 읽기는 부하가 바뀌는 동안 ±20~40 mV 튐(USB를 꽂은 일정한 부하에서는 ±7 mV).
  - **NTP:** Wi-Fi가 붙으면 `time.google.com`(→ `pool.ntp.org` → `time.cloudflare.com`)으로 시계를 맞추고 10분마다 다시 맞춤. `/api/net`의 `time:{server,tz,synced}`, 기본 `tz` 540(한국, 서머타임 없음). JC에서 `synced:true`, 화면 시계도 맞음(사용자 확인). 설정 페이지 UI에는 칸 없음(JSON으로만).
  - **혼합 빌드 `[env:jc3248w535_tls]`(Arduino를 IDF 부품으로):** TLS 버퍼를 PSRAM에. **평문 → TLS(공개 브로커) 힙 129.2 → 126.6 KB, 최저 113.8 KB 그대로**(일반 빌드는 약 46 KB 감소). 평문 기준 성능은 일반 빌드와 같음. 자세한 표·함정(IDF 기본값이 Arduino와 달라 반토막, `custom_sdkconfig` 안 먹음, `build_src_filter` 무시)은 ARCHITECTURE 11.2a. 파일: `sdkconfig.defaults`, `src/CMakeLists.txt`, `CMakeLists.txt`, `tools/partitions/default_16MB.csv`. 첫 빌드 약 5분. **지금 보드에는 이 혼합 빌드가 올라가 있음**(일반 `jc3248w535`로 되돌려도 됨).
  - **웹 서버가 부하 중 두 번 멈춤(원인 미확인, 사용자가 "나중에 보자"):** 프리셋 순환(0.5초마다 `/api/preset`)을 거는 동안 10:53(부하 시작 후 약 48분, 전압 약 3.65 V)과 11:38(서버를 켠 지 2~3분, 약 3.43 V)에 포트 80이 연결을 거부함. **두 번 모두 USB를 뽑고 배터리로만 돌 때**(10:10~11:44)였고 보드는 살아 있음(핑, MQTT 응답). 코드상 유휴 정지(요청이 있으면 갱신)로는 설명이 안 되고 `lru_purge_enable`도 이미 켜져 있음. 대조: USB를 꽂은 상태로 같은 부하를 10분(14:12~14:22), 배터리로 12분(14:22~14:34, 약 3.95 V) 걸었을 땐 안 멈춤 → **전압이 낮을 때만 멈추는지 가설, 확인 안 됨.** 진단: `-DWEB_PROBE=1` 빌드는 10초마다 자기 포트 80에 접속해 결과를 시리얼과 MQTT `<topic>/debug`로 남김(`probeWeb`). 다음에 하려면: 이 빌드를 올리고 배터리로 3.6 V 아래까지 부하를 걸며 `debug` 토픽을 받는다(지금 보드에 올라간 건 이 진단이 없는 빌드).
  - **알림은 큐(6칸)가 넘치면 가장 오래된 알림을 조용히 버림**(`engine.cpp` `enqueue`), 로그 없음. 사용자가 "나중에 보자"고 함.
  - **배터리 %와 값 다듬기(펌웨어):** `src/app/core/battery.h`의 `core::batteryPercent(mv)`(표 사이를 선형으로, 포인트: 3300→0, 3600→5, 3700→15, 3750→25, 3800→40, 3850→55, 3950→70, 4050→92, 4070→100)와 `core::MedianOfLast<N>`. **표는 일반 리튬폴리머 값에서 윗쪽만 이 기기가 충전 뒤 쉬는 4.07 V로 옮긴 것이고 이 배터리로 보정한 게 아님**(방전은 끝까지 못 가서 0%도 일반값). 펌웨어는 30초마다 읽어 최근 5개의 중앙값을 만들고 그 값과 %를 5분마다·요청 때 `{"mv","pct"}`로 보냄. 충전 중에는 전압이 높게 읽혀 %가 부풀음(4.03 V에서 85%를 봄), 부하 중에는 낮게 읽힘. 호스트에서 경계값·단조 증가·중앙값을 확인함(Emscripten + node).
  - **결정(사용자, 2026-10-29):** 혼합 빌드(`*_tls`)는 필요할 때만(릴리즈, RAM 최적화 실험) 쓰고, 기능 확인은 일반 env로 빌드한다. **CYD는 더 쓰지 않는다**(그래서 CYD의 TLS 메모리 대책은 필요 없음).
  - **혼합 빌드 확장:** `[idf_tls]` 위에 `jc3248w535_tls`, `muma_tls`, `s3lcd13_tls`(보드는 `board_build.cmake_extra_args = -DBOARD=<이름>`으로 `src/CMakeLists.txt`에 넘김; CMake 초기 스크립트 단계에서는 -D가 안 와서 그때만 기본값). **셋 다 빌드까지만 확인**(각 약 5분). muma(COM5)와 SpotPear(COM6)에는 안 올려 봄 → 부팅·힙 확인 필요.
  - **TLS 후속(2026-10-29, JC 혼합 빌드, 공개 브로커 `broker.emqx.io:8883`, 무작위 토픽):** 알림·배터리 요청·승인 카드(탭 → 응답 `option 0`)가 TLS로 주고받아짐. 연결 중 힙 117.0 → 114.5 KB(웹 켬). 상세와 **mTLS + DS 조사(가능, 시험 안 함: eFuse 굽기는 되돌릴 수 없어 시험용 기기와 별도 승인 필요)**는 ARCHITECTURE 11.2a. 시험 뒤 설정은 복구했고 공개 브로커의 retained는 지움. 시험 스크립트는 저장소 밖(`tls_round_trip.py`, `tls_cleanup.py`: `tools/mqtt/blob_mqtt.py`의 `Client`에 TLS 소켓을 끼운 것).
  - **mTLS(DS 없이, 2026-10-29, JC 일반 빌드):** 된다. 웹 `POST /api/tls/ca|cert|key`로 CA·기기 인증서·개인키를 NVS에 넣고 TLS를 켜면 자체 CA 기준으로 서버를 검증하고 클라이언트 인증서를 냄. 시험 브로커(`blob_mqtt.py broker --tls-port 8883 --tls-cert … --client-ca …`)에 붙어 카드 경로(배터리 요청 0.39 s)가 되고, 인증서 없음·다른 CA·서버 불신은 모두 `error=tls`로 막힘. 인증서는 `tools/mqtt/make_certs.py`. 자세한 건 ARCHITECTURE 11.2a. 시험 뒤 보드 설정과 NVS는 복구함. **PC에 시험 브로커가 따로 떠 있음**(평문 1884, TLS 8883, 기존 1883 브로커는 그대로): 끄려면 `Get-NetTCPConnection -LocalPort 8883 -State Listen`의 프로세스를 종료.
  - **OTA(HTTPS, 2026-10-29, JC 일반 빌드):** 된다. 시리얼 `ota <https url>` 또는 웹 `POST /api/ota`로 시작(MQTT로는 시작 안 함: 서명 검사가 없음). 받는 동안 MQTT를 멈추고, 새 이미지는 30초 돌아야 유효가 되며(그 전에 죽으면 이전 이미지로 롤백: 부팅 후 죽는 이미지로 확인) mTLS로도 성공, 실패하는 경우 6가지는 모두 원래 슬롯에 남음. 내부 힙 최저 67.5 KB(mTLS 56.2 KB). 상세는 ARCHITECTURE 11.2a. `tools/mqtt/serve_ota.py`로 시험(`-DBUILD_TAG="v2"`, `-DOTA_TEST_CRASH=1` 빌드 옵션으로 이미지를 구분·롤백 시험). **함정:** 시험 서버의 소켓 시간 제한을 20초로 두면 전송이 느린 날 서버가 끊어서 기기가 멈춘 것처럼 보임(한참 헤맸음). 보드는 표시 없는 일반 빌드로 되돌렸고 시험용 인증서는 NVS에서 지움.
  - **다음:** DS 전환(시험용 기기 한 대, eFuse 굽기는 따로 승인), 혼합 빌드에서 OTA(`sdkconfig.defaults`에 롤백 설정 추가 후 시험), 이미지 서명(보안 부팅), 다른 S3 보드에 혼합 빌드 올리기, 곡선표를 이 배터리로 보정(방전 끝까지, 가벼운 부하로 쉬는 전압 점들), 저전력 모드(밝기·렌더 정지·Wi-Fi 슬립; **IP5306은 배터리 구동 중 약 45 mA 아래가 32초 이어지면 출력을 끊음**), 웹 서버 멈춤 원인(위).
  - **함정:** `python -m esptool --port COMx read-mac | Select-Object -First 1`처럼 출력을 중간에 자르면 esptool이 끝나기 전에 끊겨 **보드가 다운로드 모드에 남음**(시리얼 무응답, 핑 없음). 출력을 `Out-String`으로 끝까지 받아서 거르거나, 다시 `--after hard_reset`으로 리셋한다. 올릴 때 MAC을 읽는 스크립트에서 이렇게 하지 말 것.
- **내부 RAM 줄이기 + 설정 메뉴 (2026-10-28, 브랜치 `heap-profile`, 커밋 `6518029` `ca2eca9`, 아직 `main`에 안 합침):** TLS를 쓸 계획이라 평문 기준 예산(≥ 100 KB)을 먼저 확보하는 작업. 측정표는 ARCHITECTURE 11.2a.
  - **웹 서버 on demand:** 소켓 2개, 부팅 10분 + 마지막 요청 5분 뒤 스스로 멈춤(JC에서 확인: 300초에 정지, 요청이 타이머를 밀어냄, 켜면 HTTP 200). 약 13 KB 반환. 시리얼 `web [on|off]`. 부팅 후 10분은 안 끈다는 조건은 시험 때 이미 10분이 지나 있어 확인 못 함.
  - **띠 버퍼 `hal::Board::stripLines`:** 기본 24, JC3248W535만 12(+23 KB, draw +1.7 ms, 대기 cpu1 +5%p). 청크·스테이지까지 줄인 구성은 카드 전환 SPI 대기가 3배라 버림. 다른 보드는 CPU 영향을 안 재서 24 그대로.
  - **설정 메뉴 + AP on demand:** BOOT 1초(모든 보드) / 시리얼 `menu` / 설정 페이지 버튼으로 엶. 터치 보정 3종, AP 모드, 닫기. 저장된 Wi-Fi가 있으면 station만으로 부팅, AP는 첫 설정·60초 접속 실패·메뉴에서만(ARCHITECTURE 9.3). 메뉴·보정 화면이 열려 있으면 뒤 화면 갱신을 멈춤. 시뮬레이터에도 같은 메뉴(AP 행은 표시만 바뀜).
  - **`HEAP_PROBE=1`** 빌드(`$env:PLATFORMIO_BUILD_FLAGS`)는 부팅 단계별 내부 힙을 찍음(`heap-mark`).
  - **슬라임 재생 테스트(2026-10-28, JC3248W535 `replay_s3`):** 이 브랜치와 태그 `slime`이 `t=`·`config` 68줄과 `done frames 2049 chain 44531f9c px 30815819 stream 6449ed96`까지 모두 같음(태그 트리에는 `src/tools/replay/`와 `[env:replay_s3]`를 복사해서 빌드). CYD(`replay`)와 다른 칩끼리는 비교하지 않음.
  - **확인 못 한 것:** AP를 켰을 때 폰 접속·설정 페이지·유휴 시 AP 내림, CYD·SpotPear·muma에서의 CPU(빌드만 확인), TLS 켠 상태에서 이번 빌드의 힙.
  - **결정할 것:** TLS·OTA 예산 숫자(제안: TLS 연결 유지 ≥ 80 KB, 핸드셰이크·OTA 최저 ≥ 40 KB). TLS는 JC에서 연결 유지 46.7 KB(24줄 기준)라 PSRAM으로 옮기거나 레코드 버퍼를 줄여야 하는데 sdkconfig가 필요 → **혼합 빌드로 해결(2026-10-29, 위 맨 앞 항목): JC에서 TLS 연결 유지 126.6 KB**. OTA 중에는 MQTT를 끊고 HTTPS를 하나만 쓰는 설계를 제안함.
  - **시리얼 열기가 간혹 끊김:** 네이티브 USB(JC `COM4`, S3)에서 포트를 열면 `ClearCommError`가 나며 다시 잡히는 일이 몇 번 있었음(원인 미확인). 올릴 때는 MAC을 읽어 확인한 뒤 올리고, 실패하면 중단.
- **집 Wi-Fi + MQTT (2026-10-23, 브랜치 `jc3248w535`):** `src/platform/esp32/network.*`. 설정 페이지 "네트워크" 칸 / 시리얼 `net [json]` / `/api/net`으로 집 Wi-Fi(STA)와 브로커를 넣으면 바로 저장·연결. 토픽 `blob/<MAC 끝 4자리>/card`(받음, `/api/card`와 같은 JSON) `/time`(받음) `/outcome`(보냄, `{"ref","option"}`) `/status`(retained, last will로 offline). 설계는 ARCHITECTURE 9.3a. 시험 도구 `tools/mqtt/blob_mqtt.py`(표준 라이브러리만: 시험용 브로커, notify/prompt/time/watch).
  - 확인한 것: 두 보드 빌드, 시뮬레이터(네트워크 칸은 `/api/net`이 없어 숨음), PC에서 시험 브로커 + 펌웨어 흉내 클라이언트로 알림·승인 요청→응답·시간·고정 카드·last will(한글 포함). JC3248W535에 올려 집 Wi-Fi·브로커 설정 없는 상태에서 CPU·힙 측정(ARCHITECTURE 11.2 "네트워크").
  - **확인 못 한 것:** 기기가 실제 브로커에 붙는 것(집 Wi-Fi 비밀번호는 사용자가 넣어야 함), TLS 연결과 그때의 힙, CYD에서의 수치.
  - 힙: S3에서 116.9 → 107.6 KB(브로커 설정 없음, CPU는 그대로). 처음엔 98.5 KB였고 네트워크 관리를 `loop`로 옮기고 mDNS를 빼서 되찾음. 브로커를 설정하면 MQTT 클라이언트가 13.9 KB 더(93.7 KB, 연결 전 재시도 상태), TLS면 내부 RAM 40 KB 안팎이 또 듦(`MBEDTLS_INTERNAL_MEM_ALLOC`, 두 보드 모두). CYD는 브로커 설정 없이 136.8 KB(가로, 2026-10-26 실측, MQTT 전 약 147 KB)라 브로커 연결 약 123 KB, TLS까지 켜면 약 83 KB로 예산 100 KB를 밑돌 것(추정) → 모자라면 줄 버퍼·카드 버퍼 등에서 찾기. 세로면 줄 버퍼가 좁아 7.7 KB 더 남음.
  - **플래시(2026-10-26): CYD 기본 빌드는 애니를 뺌**(사용자 결정: "애니는 필요할 때만 같이 굽기", OTA는 나중에 필요해서 두 칸 파티션 `min_spiffs` 유지). `-DWITH_HEROINE=0`이면 `characters.h`가 애니를 등록하지 않아 링커가 코드·그림 표를 버림: CYD 1.92 → **1.44 MB(73.3%)**, 정적 RAM 94.4 → 90.7 KB. 애니가 필요하면 env `cyd2usb_anime`(97.8%, 남은 약 43 KB). 16 MB 보드(JC, muma, SpotPear)와 시뮬레이터·재생 테스트는 기본값 그대로 애니 포함.
  - **첫 접속 안내 카드는 `sticky`**(사용자 요청): 자동으로 안 닫히고 탭해야 넘어감. 터치가 없는 보드(muma)에서는 영영 화면을 가리므로 예전처럼 6초 뒤 닫힘(`gBoard->touch`가 없으면). 카드 JSON의 `"sticky":true`로 누구나 쓸 수 있음(승인 요청이면 만료도 없음). 엔진 변경은 이 플래그 하나. 슬라임 재생 테스트(S3 `replay_s3`) `done frames 2049 chain 44531f9c px 30815819 stream 6449ed96`로 기준값과 같음(2026-10-23).
  - 시리얼 `sleep`: 백라이트를 끈 채 고정(`Backlight::holdOff`, 딥 슬립 중 핀이 떠서 켜지지 않게)하고 깨어날 조건 없는 딥 슬립. RST나 전원으로만 깨어남(사용자: 작업이 끝나면 화면이 안 뜨게).
  - 부팅 배너에 리셋 원인(`reset: power-on|panic|brownout|...`).
  - **JC3248W535 리셋 반복(2026-10-23):** 업로드 뒤 1.16초마다 `power-on` 리셋(패닉 아님). ROM 다운로드 모드(`flash_id`)는 정상, 부팅 배너까지 가고 곧 꺼짐. 포트를 열지 않고 USB 목록만 봐도 같음(`list_ports`로 MAC 찾기). 같은 방식으로 이전 펌웨어(HEAD)를 올리면 30초에 1번, mDNS를 뺀 빌드는 2번이었지만, 그 빌드도 나중에 다시 1.16초 반복 → **펌웨어가 아니라 보드 전원(사용자: USB 접촉 불량)이 오락가락하는 것.** 무선 송신(브로커 연결 시도 등)이 있으면 더 자주 리셋됨. 케이블·포트를 바꿔야 함. 반복 중에도 `sleep`은 포트가 보이자마자 계속 보내면 들어감(작업 끝에 그렇게 재워 둠). 이 보드는 S3라 `bootlog.py`(RTS 리셋)를 쓰면 USB가 끊겨 못 읽음 → `serial_cmd.py`로 리셋 없이 읽기.

- **펌웨어 하나에 캐릭터 네 개**(Capri는 2026-10-30, 맨 위 항목). 웹 설정 맨 위나 시리얼 `char <이름>`으로 바로 전환합니다.
  - **슬라임(slime):** 완성·고정. git 태그 `slime`(커밋 `cfd3f52`), 바로 올리는 이미지 `releases/slime/cyd2usb-slime.bin`(주소 0x0, NVS까지 초기화). 지금 코드의 슬라임은 태그와 **비트 단위로 같습니다**(재생 테스트, [5장](#5-테스트-방법과-함정)).
  - **픽셀 몬스터(monster):** 새 캐릭터. 포켓몬 같은 2D 픽셀 몬스터("짭켓몬")이고, 스타팅 세 종(불꽃 엠버, 풀 잎새, 물 방울) 중 하나를 고릅니다. 그림은 1세대 스타팅의 원형(불꽃 꼬리 도마뱀, 등에 식물이 난 네발 친구, 거북)만 빌린 오리지널이고, 셋 다 목도리를 둘렀습니다(실제 포켓몬 그림은 쓰지 않음). 사용자가 "일단은 이대로"라고 해서 그림과 연출은 1차본 그대로입니다. 태그 `monster`(애니와 같은 커밋). 릴리스 이미지는 아직 없음.
  - **애니(미연시 히로인, ID `heroine`): 1차본, 태그 `anime`.** 설정 페이지에는 "애니"로 나오고, 코드·설정 키·시리얼 이름은 `heroine` 그대로입니다. 릴리스 이미지는 아직 없음. 미연시(비주얼 노벨) 화면: 방 창가에 선 바스트업 초상화, 알림·승인 요청·선택지·대답이 모두 하단 대화창에 나옵니다. 그림은 사용자가 GPT 이미지 모델로 만든 것(긴 흑발, 크림색 니트)이고, Claude가 누끼·표정 프레임을 만들고 자세를 PC에서 미리 그립니다(기기는 고르고 행을 옮기기만 함). 자세한 건 아래 "미연시 히로인" 절.
    - 처음에 Claude가 SVG 좌표로 직접 그린 벡터 캐릭터는 사용자가 거부했습니다(메모리 `feedback_character_art`). 그때 만든 벡터 리그 런타임은 지웠고, 방 배경 SVG만 남아 PC에서 미리 그립니다.
- **한글:** 갈무리11(OFL)에서 변환한 픽셀 폰트입니다. KS X 1001 완성형 2,350자, 자모, 기호를 담았습니다. 카드 제목, 본문, 선택지가 한글이 됩니다(UTF-8).
- **시계:** 오른쪽 위 `HH:MM`입니다. 기기에 시간이 없어서 설정 페이지가 폰·PC 시간을 빌려줍니다(열 때 자동, "시계 맞추기" 버튼). 시리얼은 `time HH:MM`입니다. 시간을 받기 전에는 숨기고, 재부팅하면 다시 받아야 합니다.
- **브랜치:** 히로인은 `heroine` 브랜치에서 커밋해 main에 합쳤습니다(fast-forward). 공유 코드를 바꿔서 슬라임 재생 테스트를 했고 태그와 68줄 모두 같았습니다(2026-10-22). 원격 저장소는 없습니다.
- **기기에 올라가 있는 것:** 히로인의 "미리 그린 자세" 버전이고 히로인이 선택·저장돼 있습니다. 사용자 확인: "거의 완벽하다". 코어1 대기 2~5%, 대화창 5~29%, 여유 힙 147 KB, 정적 RAM 88.9 KB, 플래시 83.1%. 확인받을 것: 기울임 7단계가 튀어 보이는지, 곁눈질 눈이 튀는지.
- **시뮬레이터:** 사용자 요청으로 `python tools/web/sim.py --host 0.0.0.0`로 LAN에 열어 두었습니다(http://172.30.1.47:8766). 다른 기기에서 열리려면 사용자가 PC 방화벽의 python.exe 차단 규칙을 풀어야 합니다.
- **다른 세션과 COM3:** 같은 보드를 여러 세션이 씁니다. 업로드나 시리얼 전에 다른 세션에 알리고 번갈아 씁니다(사용자 결정).
- **화면 방향 설정 (2026-10-26, 사용자 요청 "설정에 화면 회전, 세로 버전"):** 설정 페이지 "화면 방향"(가로 / 세로 / 가로 뒤집기 / 세로 뒤집기), `/api/display`, 시리얼 `rotate 0|90|180|270`. 저장하고 기기가 다시 시작함. 설계는 ARCHITECTURE 10.1. 터치 보정은 보드 원래 방향으로 저장하고 돌려 씀.
  - JC3248W535(S3)에서 네 방향 모두 확인(오류 없음, 수치는 ARCHITECTURE 10.1): 세로 28 fps, 그리기 7.5 ms, **코어1 29~32%(가로 37~40%), 여유 힙 122.8 KB(가로 107.4 KB, 줄 버퍼가 좁아서)**. 270도는 행 뒤집기로 SPI 대기 +0.4 ms.
  - 시뮬레이터에서 세 캐릭터 모두 세로로 확인(320×480): 슬라임은 카드가 아래로, 몬스터 배틀은 위 상대·아래 내 몬스터·메뉴, 애니는 방이 폭을 채우고 대화창이 아래. 코드 변경 없이 원래 배치 그대로.
  - 기기에 올려 둔 것: JC3248W535에 **세로(90도)**. 사용자 확인: 화면이 바로 보이고 터치도 맞는 자리에 닿음(2026-10-26). CYD에도 올려 세로(90도)로 둠: 240×320, 27.5 fps, 그리기 4.8 + SPI 3.1 ms, 코어1 21~24%(가로 22~23%), 여유 힙 144.5 KB(가로 136.8 KB). CYD 세로에서 감압식 터치 정상(사용자 확인, 2026-10-26). muma는 빌드만.
  - 처음 올렸을 때 세로 화면이 안 바뀌었던 원인: `main.cpp`의 `TimedDisplay`가 새 힌트 `prefersFullRows()`를 넘기지 않았음. 디스플레이 힌트를 더하면 감싸는 쪽도 같이 고칠 것.
  - 슬라임 재생 테스트(컴포지터를 고침): S3에서 66줄 + `done frames 2049 chain 44531f9c` 모두 같음.
- **네 번째 보드 SpotPear ESP32-S3-LCD-1.3 (2026-10-26, 프리즘 "미니 TV", MAC 90:70:69:f5:84:a0, CH343 → COM6):** 사용자 요청으로 슬라임을 올림. env `s3lcd13`, 보드 `src/boards/s3lcd13/`. 240×240 ST7789(SPI2: SCLK 40, MOSI 41, CS 39, DC 38, RST 42, 메모리 240×320), 백라이트 GPIO20(NPN, HIGH = 켜짐; S3의 USB D+지만 USB는 CH343이라 비어 있음), 터치 없음. 회로도(SpotPear `ESP32S3_1.3inch.pdf`)에서 읽은 것: **QMI8658 IMU** SDA 47 / SCL 48 / INT1 46 / INT2 45(아직 안 씀), SD 16(MISO)·17(CS)·18(MOSI)·21(CLK), WS2812 GPIO15, 배터리 ADC GPIO6, GPIO19 = `RST_Control`(전원 전환 회로, 건드리지 않음). 시리얼은 UART0(CH343), `bootlog.py`로 리셋해 읽을 수 있음.
  - 수치: 슬라임 대기 28 fps, 그리기 4.5 + SPI 2.8 ms, 코어1 18~19%, 여유 힙 184 KB.
  - 기기에 둔 설정: **180도 + 좌우 반전**(NVS). 프리즘 너머로 시계 글자가 똑바로 읽힘(사용자 확인). SpotPear 데모도 프리즘판은 거울상으로 그림(TFT_eSPI `setRotation(5)`).
  - 처음엔 화면이 까맸음(백라이트만 켜짐): 리셋 핀 GPIO42가 S3에서 JTAG(MTMS) 기능으로 시작하는데 `MipiDcsSpiDisplay`가 `gpio_set_direction`만 해서 핀이 안 움직였음 → `gpio_config`로 고침(아래 함정). 이어서 80행 오프셋(제조사 데모 Arduino_GFX 값)을 넣었더니 위 1/3이 멈춘 화면 → 이 패널은 MADCTL 0에서 메모리 0~239행이라 오프셋 0이 맞음.
  - 제조사 데모는 Waveshare `ESP32-S3-LCD-1.3-Demo.zip`(170 MB)에서 범위 요청으로 설정 파일만 받아 봄(사용자 허락).
  - **자이로(QMI8658) 사용(2026-10-26, 사용자 요청 "1, 2번 다"):** 기울이면 슬라임이 내리막으로 미끄러지고 쏠리며 출렁임, 흔들면 알림이 닫힘(카드가 없으면 다음 테스트 카드, BOOT와 같은 차례: 버튼이 없어 시험하기 귀찮다는 사용자 요청), 왼쪽/오른쪽으로 기울여 0.7초 버티면 승인 요청의 첫/마지막 선택지. 사용자 확인: 흔들기로 닫힘, 기울여서 승인됨. 설계·상수·실측은 ARCHITECTURE 10.2. 칩 축은 기기에서 자세 두 개(오른쪽 내림, 내 쪽 내림)로 잼: x·y 부호 뒤집음. 위아래 느낌(내 쪽을 내리면 그림 아래로)은 사용자 확인 전.
  - 슬라임 재생 테스트(공유 코드 `BlobAnimator`·`View`를 고침): 이 보드에서 새 env `replay_s3_uart`(USB가 CH343 → UART0인 S3용)로 돌려 66줄 + `done frames 2049 chain 44531f9c` 모두 S3 기준값과 같음(2026-10-26).
  - 원래 펌웨어 전체 백업: `backup/other_asfound_16MB_907069f584a0.bin`(기기 MD5와 대조 일치, `673460ce…`).
- **세 번째 보드 Spotpear muma 1.54 (2026-10-26, 사용자의 샤오즈 AI 기기, MAC 7c:0c:5f:54:d4:4c, COM5):** 사용자 요청으로 슬라임 화면을 올려 둠. env `muma`, 보드 `src/boards/muma/`, 터치 드라이버 `drivers/esp32/cst816_touch.*`(10-23에 만들어 `backup/muma-port.patch`로만 두었던 것을 트리에 넣음), 시뮬레이터 `?board=muma`. 240×240 ST7789, 백라이트 IO42 LOW = 켜짐(`LedcBacklight` `activeLow`, `holdOff`도 따름), IO3 = 전원 유지. **이 기기는 터치 층이 없음**(I2C 무응답): 화면만. 샤오즈로 되돌리려면 전체 이미지 `backup/xiaozhi_hardened_16MB_7c0c5f54d44c.bin`을 0x0에, 덮어쓰기 직전 NVS(0x9000~0xFFFF)는 `backup/xiaozhi_nvs_0x9000_7c0c5f54d44c_20261026.bin`.
- **두 번째 보드 JC3248W535(ESP32-S3) (2026-10-22):** env `jc3248w535`, 보드 `src/boards/jc3248w535/`, 드라이버 `drivers/esp32s3/axs15231b_qspi_display.*`(S3 전용: GDMA가 PSRAM 접근), `drivers/esp32/axs15231b_touch.*`, 초기화 표 `drivers/panels/axs15231b.h`. 480×320 가로. 시뮬레이터는 `http://localhost:8766/?board=jc3248w535`(세 캐릭터 모두 그대로 동작, 애니의 방 그림은 320×240이라 가운데 액자처럼 보임).
  - 하드웨어: ESP32-S3 rev0.2, 16 MB 플래시, 8 MB 옥탈 PSRAM, 네이티브 USB(USB-Serial/JTAG) **COM4**, MAC 28:84:85:53:4c:08. 화면 AXS15231B 320×480 QSPI(SCLK 47, D0~D3 21/48/40/39, CS 45, TE 38), 터치는 같은 칩 I2C 0x3B(SDA 4, SCL 8), 백라이트 1, BOOT 0. 핀·초기화는 `refs/JC3248W535EN`(사용자가 준 저장소).
  - 공장 펌웨어 백업 `backup/factory_full_16MB_288485534c08.bin`(기기 MD5와 대조 일치, 복원은 README).
  - 화면: QSPI에서는 행 창이 없어 매번 맨 윗줄부터 씀. **바뀐 행까지만 보내면 잔상이 남았음**(사용자 사진) → 매번 프레임 전체를 보냄(제조사 BSP와 같음). 구조(드라이버 헤더 주석에 자세히):
    - 컴포지터가 이 디스플레이에만 화면 높이 전체의 세로 띠를 줌(`prefersColumnStrips()`). 화면 세로줄 = 패널 행이라 flush()(코어1)가 16열씩 내부 RAM에서 패널 행으로 돌리고(2×2 32비트 전치), GDMA가 PSRAM 뒷 프레임에 씀. CPU는 PSRAM 프레임을 건드리지 않음(캐시를 거친 PSRAM 쓰기가 느렸음).
    - PSRAM 프레임 2장. present(): 지난 프레임이 바꿨고 이번 프레임이 안 그린 행만 GDMA로 앞→뒤 복사, 지난 프레임 전송이 끝날 때까지 잠들어 기다림(패널보다 빨리 그리지 않음), 교체, 송신 태스크를 깨움.
    - 송신(코어0): TE 하강(스캔 시작)을 기다려 GDMA로 16행씩 내부 버퍼 2개에 번갈아 옮기고 SPI DMA로 보냄(IDF 4.4 spi_master는 PSRAM에서 직접 DMA 못 함).
    - TE(IO38) 측정: high(V-blank) 2.69 ms, low(스캔) 13.92 ms, 60.2 Hz. 40 MHz에서 한 프레임 전송이 16.2 ms(최대 16.7)로 스캔보다 느려, 스캔 시작에 보내면 끝까지 스캔 뒤를 따라감 → 찢어짐 없음(사용자 확인, 제조사 BSP도 하강 엣지). V-blank 시작(상승)에 보내면 마지막 행에서 스캔이 따라잡을 수 있음.
    - **80 MHz는 안 됨:** 그림이 대각선으로 밀리고 오른쪽(프레임 뒤쪽)으로 갈수록 심해짐(사용자). 데이터시트는 쓰기 클럭 주기 11 ns(약 90 MHz)까지 허용하지만 이 보드 배선으로는 깨짐.
    - 하드웨어 회전(MADCTL MV)은 안 됨: 검은 화면 + 왼쪽 한 줄만 하얗게(창 좌표 두 가지 다 시험). QSPI에서 행 창이 안 되니 MV도 못 쓰는 것으로 보임. 세로는 패널 본래 방향이라 회전이 필요 없음(아래 "화면 방향").
  - 수치(40 MHz): 슬라임 대기 28~30 fps, 그리기 8.8 ms + 회전·대기 1.6 ms, **코어1 34~37%, 코어0 5~6%**, 여유 힙 117.6 KB. `stress`(매 프레임 화면 전체): 약 41 fps, 코어1 약 66%(대기 시간 대부분은 잠듦), 코어0 6%. 부팅 직후 애니메이션 47 fps, 코어1 78%.
    - 전체 갱신이 60 fps가 안 되는 이유: 전송 16.2 ms가 주기 16.6 ms와 거의 같아 다음 스캔을 자주 놓침. 80 MHz(전송 8.5 ms)일 때 56 fps였지만 코어1 91%. 60 fps 전체 갱신은 그리기 비용만으로 코어1 80~90%.
    - 거쳐 온 단계(`stress`): CPU가 PSRAM에 회전해 쓰기 → 회전 18.6 ms, 코어1 99% / 세로 띠 → 15.4 ms / GDMA로 PSRAM에 쓰기 + 2×2 전치 → 2.8 ms, 57 fps, 코어1 94%(TE 없이 70 fps 넘게 그림) / 이중 버퍼 + TE + 기다림 → 36 fps(매 프레임 307 KB 전체 동기화 복사 탓) → 필요한 행만 복사 → 41 fps, 66%.
    - Xtensa GCC 8.4: 회전 루프에 변수가 많으면 픽셀마다 스택에 넘침(2×2 첫 버전은 오히려 10.2 ms). `__restrict` 작은 함수로 나눠 2.8 ms.
  - 터치: 손을 떼면 처음엔 `00…`이지만 한 번 눌린 뒤엔 `60 60 …`/`ff ff …` 채움 값을 보냄 → 점 개수 정확히 1 + "떼어짐"(이벤트 1) 제외로 고착 해결. 좌표 매핑은 참고 저장소 그대로(x = 원시 y, y = 319 − 원시 x)가 맞고, 터치가 화면보다 가로로 약 8% 넓게 퍼져 있어 **보정값**(아래 "터치 보정")으로 맞춤: `x = 0.9239·X + 18.95`, `y = 0.9600·Y − 0.45`(72탭, rms 10.9 px). 이 값으로 카드 버튼이 눌림(사용자 확인).
    - 측정에서 본 것: 첫 좌표 / 처음 3개 평균 / 중앙값 / 마지막 좌표의 오차 차이는 0.1 px 정도라 탭은 지금처럼 닿는 순간 처리. 누르는 중 끊김 최대 약 20 ms(뗌 판정 30 ms 이상이면 충분), 가장 짧은 탭 약 60 ms, 보통 60~150 ms.
  - 오른쪽 잘림: 보정 화면의 가장자리 선(0/4/9 px)이 네 변 모두 보임(사용자 확인) → 해결됨.
  - 하드웨어 자료 `refs/JC3248W535-hw/`(SOURCES.md에 출처). 회로도는 `refs/JC3248W535EN/documentation/JC3248W535-1/2.png`뿐(BOM·PCB 없음). 회로도에서 읽은 것(기기 미확인):
    - 패널 리셋은 ESP32 EN에 묶임(GPIO 없음) → 소프트 재시작으론 패널이 리셋되지 않으니 SWRESET 필수(드라이버가 보냄). TE=IO38.
    - 터치는 AXS15231B 내장, SDA 4 / SCL 8(4.7k 풀업, 회로도 라벨은 반대로 오기), INT는 0Ω R17로 IO3(실장 여부 미확인), 터치 리셋 없음.
    - 백라이트 IO1 HIGH = 켜짐(N-FET, 10k 풀다운, PWM 가능). USB는 네이티브(IO19/20), **CC 저항 없음**으로 보임(C-to-C 전원 안 될 수 있음).
    - 충전 IC IP5306: 배터리 동작 중 부하 45 mA 미만 32초면 출력 차단(절전 설계 때 주의). 배터리 전압 IO5(분압 ×1.33, P2 헤더와 공유).
    - 앰프 NS4168: BCLK 42, LRCLK 2, DIN 41, 오른쪽 채널 고정. SD: CS 10, MOSI 11, CLK 12, MISO 13. 헤더 P2: IO5,6,7,15,16,46,9,14 / P3·P4: IO17,18.
  - 공유 코드 변경: `hal::Display::present()`(프레임 끝, 기본 아무것도 안 함, `Compositor::render()` 끝에서 부름)와 `prefersColumnStrips()`(기본 false; 참이면 컴포지터가 더러운 영역을 화면 높이 전체로 넓히고 x 순으로 합침). 슬라임 재생 테스트: 같은 S3에서 태그와 지금 빌드가 66줄 + `done` 모두 같음(env `replay_s3`).
  - **다음:** 코어1을 더 줄이려면 그리기 비용 쪽: 슬라임 그리기 최적화(화면 해시는 그대로) 또는 fps 상한(사용자: 최후 수단).
- **터치 보정(모든 보드):** `src/app/ui/touch_calibration.*`. **2026-10-28부터 보정 화면 앞에 설정 메뉴가 있습니다**(`src/app/ui/settings_menu.*`): 1. 터치 보정 / 2. 롱 프레스 보정 / 3. 드래그 보정 / AP 모드 / 닫기(사용자 요청: 보정 메뉴가 아니라 설정 메뉴, 보정 단계와 AP 모드는 분리). 보정 화면은 종류를 받아 바로 시작하고(`start(cal, Kind)`), 끝나면 메뉴로 돌아옴. 메뉴와 버튼은 지금 보정값으로 누름. 진입: 시리얼 `menu`/`touchcal`, 웹 `POST /api/touchcal`(설정 페이지 "터치 보정" 버튼), BOOT 1초 길게 누르기. 메뉴에서 BOOT 짧게 = 다음 항목, 길게 = 선택(터치가 너무 어긋났거나 없어도 됨), 보정 중 BOOT = 취소. 셋 다 끝나면 저장(초록)/다시/취소. 플랫폼은 보정 화면이 떠 있는 동안 매 프레임 원시 값과 동작을 `onTouch`로 넘김.
  - **1. 터치 보정:** 과녁 9곳을 돌며 누를 때마다 축마다 `화면 = a·원시 + b`를 최소제곱으로 다시 맞추고(퍼짐이 60 px 미만이면 오프셋만), 빨강(원시)·초록(보정) 점을 보여 줌. 9탭 한 바퀴 동안 1.5 px 미만으로만 변하면(최소 18탭) 버튼이 뜸. 과녁에서 화면 폭/8 넘게 빗나간 탭은 버림. 원시 좌표만 써서 기존 보정이 틀어져 있어도 쓸 수 있음.
  - **2. 롱 프레스 보정:** 왼쪽 +를 평소처럼 톡 8번(1초 넘는 건 안 셈), 이어서 길게 꾹 5번(0.2초 미만은 안 셈). 누른 시간 = 첫 닿음부터 마지막 닿음까지(뗌 판정 제외). 기준 = 가장 긴 톡과 가장 짧은 꾹의 중간, 가장 긴 톡 + 100 ms 이상, 250~1500 ms. 시뮬레이터: 톡 최대 134 ms, 꾹 최소 700 ms → 417 ms.
  - **3. 드래그 보정:** 왼쪽 +를 움직이지 말고 10번 눌렀다 뗌(톡·꾹 섞어서). 흔들림 = 내린 자리에서 축별 최대 거리(원시 px, `TouchGesture`와 같은 잣대), 48 px 넘게 움직인 누름은 드래그로 보고 안 셈. 기준 = `최대 흔들림 × 1.5 + 2`(10~40 px). 회색 상자 = 지금 기준, 초록 상자 = 새 기준, 빨간 점 = 손가락 길. CYD 감압식 흔들림도 이걸로 재면 됨. 뗌 판정(50 ms)은 이미 잰 값이라 보정 안 함.
  - 값: `hal::TouchCalibration`(보드 기본값 `hal::Board::touchCalibration`, CYD는 보정 없음; `slopPx` 기본 16, `longPressMs` 기본 500) → NVS `cyd/touchcal`(JSON `{"ax","bx","ay","by","slop","hold"}`)이 있으면 그 값(예전 JSON에 없는 키는 기본값). 각 보정은 자기 값만 바꿔 저장. 플랫폼이 읽은 직후·저장 직후 적용(`ui::applyTouchCalibration`, `TouchGesture::tune`), 시뮬레이터는 localStorage `cyd.touchcal`.
- **터치 동작(모든 보드):** `src/app/ui/touch_gesture.*`가 프레임마다 읽은 값을 탭/길게 누르기/드래그로 바꿈(플랫폼 두 곳이 같이 씀). **탭은 뗄 때** 처리(0.5초 전에 떼면). 처음엔 닿는 순간 탭 + 0.5초 뒤 길게 누르기였는데, 길게 누를 때도 찌르기(통통 튐)가 같이 나와서 바꿈(사용자). 그래서 탭 반응이 누른 시간 + 뗌 판정만큼 늦음. 손가락이 드래그 기준(기본 16 px, 보정 메뉴의 "드래그 보정"으로 재서 저장) 안에서 롱 프레스 기준(기본 0.5초, "롱 프레스 보정") 동안 머물면 길게 누르기(탭 없음) → 캐릭터 위면(카드·대화창 제외, `Character::hitTest`가 자기 상자를 뺌) 다음 표정(기쁨 → 놀람 → 졸림 → 화남 → 슬픔)을 2.5초 미리보기로 보여 줌(사용자 요청). 16 px 넘게 움직이면 드래그(아래). 뗌: 기기 50 ms 동안 안 닿음(누르는 중 끊김 최대 약 20 ms), 시뮬레이터는 바로. 터치 보정 과녁도 탭(뗄 때, 닿은 자리)으로 받음. 슬라임 재생 테스트 같음(S3, 68줄).
  - **드래그(사용자 요청: 캐릭터 잡아 옮기기, 놓으면 원래 자리로):** 16 px 넘게 움직이면 드래그 시작(길게 누른 뒤면 `HeldDragStart`) → 매 프레임 드래그 → 떼면 끝. 플랫폼은 동작을 `View::handleGesture`에 넘기기만 함. 캐릭터 위(카드·대화창 제외)에서 시작하면 `Character::grab(x, y, held)/drag/release`; `held` = 길게 누른 뒤.
    - 슬라임은 동작으로 갈림(사용자 결정, 한때 있던 설정 칩 `motion.drag`는 없앰): **길게 누른 뒤 끌기 = 들고 다니기**(아래 첫 설명), **그냥 끌기 = 늘리기·줄이기**(아래 "stretch"). 길게 누르면 슬라임만 표정 미리보기 대신 살짝 움찔(`Character::longPress` → `BlobAnimator::flinch`: 위에서 누르듯 세로로 짧게 찌그러졌다 돌아옴, 높이 −9% 약 0.3초, `kFlinchSquash`): 0.5초가 됐으니 이제 끌면 들린다는 신호(사용자 요청, 표정 바꾸기는 원래 동작 확인용이었음). 몬스터·애니는 길게 누르면 그대로 표정 미리보기. 들고 다니는 동안 슬라임은 놀람 표정(`kCarriedFace`, 사용자 요청 "표정 하나만"; 놓으면 원래 표정으로). 사용자가 "놀람이 크게 안 놀라 보인다"고 해서 JC3248W535에 저장된 놀람 눈만 키움(w 0.40→0.46, h 0.48→0.58, 안광 1.2→1.3; 코드 기본값과 슬라임 태그는 그대로).
    - 들고 다니기: 몸이 손가락에 단단한 스프링(ω 28, 4번 나눠 계산)으로 따라오고, 움직이는 동안은 꼬리 스프링이 미끄러질 때처럼 뒤로 늘어짐. 느껴지는 가속은 120 R/s²로 제한(빠른 손가락은 미끄러짐보다 훨씬 셈). 놓으면 앵커와 어긋나 있으니 원래의 "스~~윽 텅"으로 돌아감. 잡힌 동안 숨쉬기·둥실 없음.
      - **늘리기·줄이기(stretch)**: 몸 중앙은 그대로, 잡은 곳과 그 둘레가 손가락을 따라감. 놓으면 0.12초 시간상수로 되돌아감(`kSnapBackS`). 잡힌 동안 숨쉬기·둥실 없음.
        - **stretch의 지금 방식**(`FluidBody::Input::dragX/dragY/dragAngle`, `FluidBody::dragOutline`): 드래그를 시작한 표면 지점(`pullRoot_`, 가운데를 잡으면 처음 당긴 쪽)의 목표 외곽점을 손가락이 끈 만큼 옮기고, 둘레의 점은 그 자리에서 멀어질수록 덜 옮김(가중치 `((1+cos Δθ)/2)^3`, 50°에서 절반, 90°에서 1/8: `kDragBreadth`). 옮긴 외곽선을 각 마디의 방사선 위에서 다시 읽음(접히면 가장 먼 교점). 끈 거리는 바깥 방향과 옆 방향으로 나눠 각각 `tanh`로 막음: 바깥 0.8R, 안쪽 0.45R, 옆 0.8R(`kPullOut`, `kPushIn`, `kPullSide`; 옆은 처음 0.6R에서 사용자 요청으로 키움, 1.0R까지도 외곽선이 안 접힘). 끄는 동안 몸 전체 넓이 유지(`keepArea`): 당기면 반대쪽이 가늘어지고 밀면 옆이 부풂. 프레임 테스트(끈적한 설정): 오른쪽 끝을 120 px 당기면 그 자리 +29 px·반대쪽 −11 px, 넓이 변화 1% 안, 손가락을 위로 돌리면 가장 나온 자리가 −28° → −51°로 따라감, 머리를 아래로 끌면 위가 약 20 px 눌리고 멈춤.
        - 거쳐 온 길(사용자 피드백 순서): 꼬리를 손가락 방향으로 → "드래그 위치가 바뀌면 그곳이 당겨짐"(늘어나는 자리가 몸 둘레를 미끄러짐) → 시작 자리에 고정 + 0.3 rad만 기울임 → "옆으로 돌려도 안 따라온다" → 시작 자리에서 손가락 쪽으로 도는 따로 그린 가닥(둥근 원뿔) → "너무 한 부분만 끌려온다" → 지금의 넓게 따라오는 방식. 그 사이 "부피 유지가 안 된다"(가닥을 몸 위에 얹어 넓이가 늘었음) → 몸 넓이 유지로 바꿈, "머리를 잡고 아래로 끌면 위로 꼬리가 솟음" → 안쪽은 눌리기만 하고 반대편으로 안 나감.
        - **크기 유지**(몸째 따라옴): 유체 몸은 넓이를 지키는데 목표 꼬리 넓이를 넣고 실제 꼬리는 그만큼 못 늘어나서, 잡는 동안 부풀고 놓은 직후 줄었음(+21% → −22%). 잡힌 동안·놓고 집에 돌아가는 동안(`returning_`) 꼬리 마디를 빼고 나머지만 제 넓이를 지킴(`tailApart`, 꼬리는 그 위에 얹힘), 높이 변화 ±2.5% 안. 같은 부풀었다 줄어듦은 일반 이동(카드가 뜰 때 미끄러짐)에도 있음: 고정된 슬라임이라 그대로 둠.
      - 끌어 보고 사용자가 "더 끈적하게"를 골라, JC3248W535에 저장된 슬라임 설정값만 바꿈(코드 기본값과 슬라임 태그는 그대로): 점성 5→12, 복원력 160→90, 출렁임 0.7→0.9, 꼬리 늘어남 1.6→2.2, 이동 시간 260→380 ms, 도착 충격 0.65→0.35. 사용자: "이정도면 된 거 같고".
    - **CYD 감압식에서 드래그가 끊기던 문제**(사용자: "드래그가 잘 안 된다", 2026-10-23): XPT2046 드라이버가 앞 샘플과 원시값 150 넘게 다른 샘플을 튐으로 버리는데, 빠른 드래그는 매 프레임(약 16 ms) 그 이상 움직여서 계속 버려지고, 50 ms 넘게 이어지면 뗀 것으로 봐서 드래그가 여러 토막(과 가짜 탭)으로 갈렸음. 측정(임시 카운터, 약 24초 끌기): 받은 샘플 954개에 튐으로 버린 것 218개(약 19%), 샘플 간 최대 362, 드래그 시작 27·탭 16. 고침: 앞 샘플 근처이거나 앞 걸음을 그대로 이은 자리 근처면 받음(`Xpt2046Bitbang`의 `stepA_/stepB_`; 한 번 튀는 잡음은 둘 다 아니라 그대로 걸러짐). 다시 재니 튐 9개(0.6%), 최대 580, 가짜 탭 0, 누름마다 드래그 하나. 사용자 확인 "문제없다".
    - 몬스터: 필드에서만. 잡으면 깨어나 "!", 손가락 바로 아래(머리가 5칸 아래, 손에 안 가리게: 사용자 요청)에 매달려 공중에 뜸. 그림자는 6칸 아래 땅에 남고 작아짐(`SpriteLayer`의 `air`). 매달린 동안은 바둥 스프라이트 2장(`SpeciesArt::held`, 눈 질끈 >< + 입 벌림 + 팔다리 번갈아; 걷기 아래 방향 그림에서 고침)을 120 ms마다 번갈아. 놓으면 0.16초 만에 떨어지고 잡혔던 자리로 걸어서 돌아감. 상수는 `kHeldAir`, `kHangGap`. 애니: 창가에 고정이라 안 잡힘(기본 `grab` = false).
    - 시뮬레이터에서 확인(프레임 모음). 슬라임 재생 테스트 같음(S3 `replay_s3`, 태그와 68줄 모두, 2026-10-23; 잡히지 않으면 계산이 같도록 `held_` 조건만 더했음). JC3248W535에서 사용자 확인(끈적하게 바꾼 슬라임, 몬스터 들기 모두 괜찮음). CYD는 아직 안 올림: 감압식 흔들림이 16 px 안에 드는지 CYD에서 확인할 것.

## 2. 하드웨어와 환경

| 항목 | 값 |
|---|---|
| 보드 | ESP32-2432S028R **CYD2USB** (USB-C + micro-USB), MAC b0:cb:d8:c3:32:94 |
| 칩 | ESP32-D0WD-V3 rev3.1, 240 MHz × 2코어, 4 MB 플래시, PSRAM 없음 |
| 화면 | 2.8" 320×240 ILI9341 호환, HSPI(SCLK 14, MOSI 13, CS 15, DC 2), 백라이트 21. **반전(INVON) 필요**, MADCTL 0x28 |
| 터치 | XPT2046 감압식. 비트뱅(CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36). raw 범위 x 200~3700, y 240~3800 |
| USB | CH340 → **COM3**, 업로드 **460800** (921600은 데이터가 깨짐). USB-C는 C-to-C 케이블로는 전원이 안 들어옴 |
| 버튼 | BOOT(GPIO0): 눌렀다 뗄 때마다 영어 알림 → 영어 승인 요청 → 한글 알림 → 한글 승인 요청(움직임 센서가 있으면 카드가 없을 때 흔들어도 같은 차례). 1초 길게 누르면 설정 메뉴(메뉴에서 BOOT 짧게 = 다음, 길게 = 선택) |
| PC | Windows 11, PowerShell 5.1, Python 3.11, PlatformIO Core 6.2 (`python -m platformio`), esptool 5.6, Git (PATH에 없으면 전체 경로로 실행), emsdk 6.0.13 (`C:\Workspace\emsdk`, 시뮬레이터용). 호스트 C++ 컴파일러는 없음 |
| 프레임워크 | `espressif32@^6.9.0` = Arduino 2.0.17 / ESP-IDF 4.4, C++17(-O2) |

- 공장 펌웨어 백업: `backup/factory_full_4MB_b0cbd8c33294.bin` (git에서 제외, 복원 명령은 README)
- 참고 저장소(`refs/`, git 제외): witnessmenow CYD, JC3248W535EN(ESP32-S3 보드, 이 기기와 맞지 않음), Project-Sand-Clock, esp32-robot-face(Bloub)

## 3. 코드 구조

```
src/app/          순수 C++ (플랫폼 헤더 금지, ArduinoJson만 허용)
  core/           SpscQueue, TripleBuffer, utf8.h(UTF-8 해독, 글자 단위 자르기)
  engine/         기분(Mood) + 카드 큐(알림/승인 요청) + Outcome. 픽셀을 모름. 카드 글자는 UTF-8
  gfx/            Compositor, Canvas, sdf.h, font5x7.h(영문), font(비례 폭 UTF-8 폰트, wrapText),
                  fonts/ko11.cpp(한글, 변환 결과)+LICENSE-Galmuri.txt, pixel_art.h(문자 격자 → 팔레트 그림), bench
  ui/             View(레이아웃·공유 카드·시계·HUD), layers(배경·HUD·시계·카드)
    character.h   ★ ui::Character 인터페이스 + CharacterWith<설정> (설정 우편함)
    characters.*  ★ CharacterSet: 캐릭터 등록 한 곳 + 설정 문서({"character":..,"slime":{..}}) 병합/출력
    expression.*  공통 표정 어휘(normal/happy/surprised/sleepy/angry/sad)
    touch_calibration.*  보정 화면(터치, 롱 프레스, 드래그 중 하나)과 계산(모든 보드, 설정 메뉴에서 시작)
    settings_menu.*      설정 메뉴(보정 3종, AP 모드, 닫기; 모든 보드, 플랫폼이 띄움)
    hangul.h      조사 고르기(이/가, 은/는)
    json_fields.h 설정 코덱 공용 도우미(#rrggbb, 범위 제한, 반올림)
    blob/         슬라임 전체 + slime_character(감싸기)
    monster/      픽셀 몬스터 전체(스타팅 3종 그림, 레이어, 상태 기계, 설정 코덱·프리셋)
src/hal/          Display, Touch, Backlight, Board 인터페이스
src/drivers/      esp32/, common/(XPT2046), panels/, web/(캔버스 디스플레이, 포인터 터치)
src/platform/esp32/  main.cpp(태스크·시리얼 명령·시계), config_store(캐릭터별 NVS·뮤텍스), network(집 Wi-Fi·MQTT), web_server(+web_page.h), perf_probe
src/platform/common/ api_json(meta, 카드, 시간 JSON: 웹 서버, 시리얼 '@', 시뮬레이터가 같이 씀), sample_cards(BOOT·흔들기 테스트 카드)
src/platform/web/    시뮬레이터 런타임(main.cpp, index.html)
src/boards/          cyd2usb/, jc3248w535/, web/(?board=jc3248w535면 480×320)
src/tools/hw_test/   예전 TFT_eSPI 하드웨어 테스트 (env hw_test)
src/tools/replay/    ★ 결정적 재생 테스트 (env replay): 화면 해시로 두 빌드를 프레임 단위 비교
tools/serial/        bootlog.py [PORT] [초], serial_cmd.py
tools/studio/        USB로 설정 페이지 쓰기 (serial_studio.py)
tools/mqtt/          blob_mqtt.py: 시험용 MQTT 브로커, 카드 보내기·응답 기다리기
tools/web/           시뮬레이터 빌드·개발 서버 (sim.py)
tools/font/          bdf2font.py (BDF → gfx::Font 표). 원본 폰트는 tools/font/galmuri/ (git 제외)
```

- **스레드:** 코어0 = 엔진 50 Hz, Wi-Fi, 웹 서버. 코어1 = UI(이동 중 60 fps, 대기 30 fps). loop() = 시리얼, BOOT, 응답 출력(시리얼·MQTT), 네트워크 관리(`net::poll`). MQTT 클라이언트 태스크(esp-mqtt)는 코어 고정이 안 돼서 우선순위 1.
- **데이터 흐름:** `터치 → View::handleTap → (캐릭터가 카드를 직접 그리면 Character::handleTap) → Engine 큐 → Engine::tick → WorldState → View::update → 레이어 → Compositor → DMA`
- **설정 흐름:** `웹/시리얼 → ConfigStore(뮤텍스) → CharacterSet::merge → 캐릭터별 edited_ → publishSettings(트리플버퍼) → UI 태스크 View::syncCharacter(active) → Character::syncSettings`
- **캐릭터 추가:** `src/app/ui/<이름>/`에 `CharacterWith<설정>` 구현을 만들고, `characters.h`에 멤버 하나와 목록 한 칸을 추가합니다(kCount도 같이). 웹 편집 섹션은 `web_page.h`에 `ed-<이름>`과 render 함수를 추가합니다. 그림이 커서 작은 보드에서 빼야 하면 애니처럼 빌드 플래그(`WITH_HEROINE`)로 등록을 감쌉니다: 등록하지 않은 캐릭터는 링커가 통째로 버리고, 저장된 활성 캐릭터가 없으면 0번(슬라임)으로 시작합니다.

### 미연시 히로인 (`src/app/ui/heroine/`, 그림은 `art/heroine/`)

그림 파이프라인 (PC, Python + numpy/OpenCV, 결과 표는 저장소에 넣고 펌웨어는 그대로 씀). **그림과 리깅이 한 세트(`art/heroine/`)입니다(2026-10-26, 사용자 요청 "스프라이트 뿐 아니라 리깅까지 세트로"):** 원본(`base_source.webp`), GPT 편집본(`src/`), 방(`room.svg`), 그리고 도구가 알아야 할 나머지 전부를 담은 `rig.json`(얼굴 창과 눈·눈썹·입 타원, 표정 목록과 바뀌는 영역, 자를 행·기기 크기, 고개 기울임 단계·피벗·목·머리카락 행, 가슴 행, 출력 경로, 방 시간대 색. 형식은 `tools/rig/rig.py`). 저장소에는 이 원본들만 있고, 누끼와 프레임 PNG는 변환기가 다시 만듭니다. **변환기는 빌드와 따로 돌리는 도구 하나**(사용자 요청 "변환기는 툴 따로". OpenCV가 필요하고, 전부 다시 하면 약 5분 20초: 대부분 표정 맞춤):

```
python tools/rig/studio.py                         웹에서 그림 올리고 리깅, 미리보기, 저장·변환 (http://localhost:8770)
python tools/rig/convert.py art/heroine            입력이 바뀐 단계만 (각 도구 스크립트와 rig.json 중 그 단계 몫도 입력), --force: 전부(322초)

art/heroine/base_source.webp  사용자가 준 원본(초록 배경, 1024×1536)
  └ 1. cutout.py    누끼: 크로마(초록) / 단색 / 그려 넣은 체크무늬 자동 판별 → base_cut.png, 확인 시트 base_check.png
art/heroine/src/<이름>.png    GPT로 편집한 표정 변형본(크기·위치가 원본과 달라도 됨). sheet_*.webp는 받은 그대로
  └ 2. frames.py    얼굴 찾기(다중 배율 템플릿 매칭) → 위치별 색 맞춤 → 눈·눈썹·입 영역을 원본 누끼에 붙임 → art/heroine/<이름>.png
  └ 3. portrait.py  원본을 메시로 굽혀 고개 기울임 7단계를 그림(4배 슈퍼샘플링, 기기 크기 384×252),
                    단계마다 표정 프레임은 그 자세와 달라진 픽셀만(settle로 미세한 차이는 자세 쪽으로),
                    255색 팔레트(k-means + 4×4 순서 디더), 같은 행은 한 번만 → heroine_portrait.{h,cpp} (약 20초)
art/heroine/room.svg
  └ 4. scene.py     방 배경을 시간대 4개(아침·낮·저녁·밤 색)로 미리 그림(4배 슈퍼샘플링) → 256색 RLE
                    → heroine_room.{h,cpp} (시뮬레이터는 SVG나 rig.json이 바뀌면 자동으로 다시 만듦)
```

- **리그 스튜디오(웹, 2026-10-26, 사용자 요청 "웹으로 띄워서 사진 넣고 리깅 바로 같이"):** `python tools/rig/studio.py` → http://localhost:8770 (launch 설정 `rig`). 단계별 화면(사용자 의견 "어려워 보인다", "어떤 이미지를 넣어야 하는지 명확하지 않다"를 받아 바꿈): 1 그림(예시 그림, 체크리스트, 크기·방향 경고, 이미지 모델 부탁 문장) → 2 누끼(결과 확인 시트, 덜 지워졌을 때 옵션) → 3 고개(색별 선 설명, 기울임 애니메이션 미리보기) → 4 얼굴 → 5 표정(`studio.py`의 `SLOTS` 13개 = 펌웨어 `kHeroineFrame*`: 카드마다 설명, 예시 얼굴(히로인의 맞춘 결과에서 얼굴 창만 자른 썸네일 `/thumb`), 부탁 문장 복사, 올린 그림, 맞춘 얼굴) → 6 변환(빠진 표정 목록) → 고급(숫자, 표정 목록, 방 색). 그림 위 손잡이는 그 단계 것만 보입니다. 새 세트는 13개 표정 칸으로 시작합니다. 도트 스프라이트 세트(`sprites.json`)는 공통 시트(모든 몬스터가 같이 씀) → 몬스터마다 필요한 것(시트 배치, 칸마다 그릴 것 `roles`, 정보 항목) → 몬스터별 카드(시트 두 장, 정보: 이름표·기본 이름·프리셋·기술 모양·특징 이름·기본 색, '정보 저장', '이 몬스터 빼기') → '+ 몬스터 추가'(빈 투명 시트 또는 기존 몬스터의 그림 복사, 팔레트·정보는 고른 몬스터에서) 순서로 보여 줍니다. 팔레트마다 '팔레트 내려받기(.gpl)'로 도트 편집기에 견본 색을 불러올 수 있습니다. 시트를 칸 격자·칸 이름·팔레트 견본과 함께 그리고, '바꾸기'로 올린 PNG나 저장한 정보는 바로 `sprites.convert`로 검사해 안 맞으면 되돌립니다. 확인: 스튜디오에서 thunder를 추가하고 색·기술을 바꾸니 시뮬레이터 설정의 스타팅에 '번개 · 찌릿'과 특징 이름이 나오고 노란 몬스터로 그려짐(코드 수정 없음), 빼고 나서 생성 헤더가 원래대로. 세트 고르기/새 세트, 원본·표정 편집본 올리기(끌어다 놓기, 편집본은 PNG로 저장), 그림 위에서 끌어서 리깅: 자르기 위·아래, 목 위·아래, 머리카락 시작·끝, 가슴 행, 피벗, 얼굴 창, 눈·눈썹·입 타원(가운데와 가로·세로 손잡이). 자르기·기기 높이를 바꿔도 리깅은 그림 위 그 자리에 남습니다(기기 px 값을 다시 계산). 미리보기는 저장 전 리깅으로 고개 기울임 한 단계를 기기 크기로 약 1초에 그림(`portrait.Warp` 그대로), '고개 따라오는 정도'는 `head_weight`를 빨갛게 겹침. 방 탭은 시간대 4장을 화면의 색으로 서버가 그림. 저장(Ctrl+S, 되돌리기 Ctrl+Z)은 `rig.dump` 형식(맞으면 한 줄, 아니면 항목마다 한 줄)이고 내용이 같으면 파일을 건드리지 않음. '누끼 만들기'·'변환'·'전부 다시'는 저장 후 서버에서 돌고 로그 탭에 나옵니다(한 번에 하나). 새 세트로 확인: 그림·편집본 올리기 → 변환 34초(누끼, 표정 1장, 자세), 피벗만 옮기고 변환 → 자세만 11초. 새 캐릭터를 펌웨어에 보이려면 Character 구현은 따로 필요합니다(지금 표를 쓰는 건 히로인뿐).
- **변환기는 단계마다 쓴 설정을 기억합니다(`<세트>/.convert.json`, git 무시):** 단계의 입력 = 파일 + 그 도구 스크립트 + rig.json 중 그 단계 몫(누끼: source·cutout, 표정: face·그 표정, 자세: name·namespace·portrait·frames, 방: backdrop). 그래서 피벗을 옮기면 자세만 다시 굽고, rig.json을 저장했다고 5분짜리 전체가 돌지 않습니다.
- 도구는 하나씩도 돌립니다: `frames.py art/heroine eyes_half`, `portrait.py art/heroine`, `scene.py --all`, `cutout.py <그림>`(세트와 무관한 일반 도구).
- 세트로 옮긴 뒤 `convert.py --force`가 누끼·프레임 PNG와 `heroine_portrait.*`를 비트 단위로 똑같이 다시 만듦을 확인(`heroine_room.h`는 첫 줄 주석의 SVG 경로만 바뀜). 다시 만들어도 같은 입력이면 같은 출력입니다(k-means 시드 고정).

| 파일 | 역할 |
|---|---|
| `heroine_character` | 상태 기계: 카드(알림=대사, 승인 요청=질문+선택지, 대답 연출), 표정(Look → 프레임), 3단 깜빡임, 곁눈질, 말할 때 입 프레임, 쓰다듬기/찌르기, 졸기. 자세: 고개 기울임은 스프링 값을 가장 가까운 그려 둔 단계로(반 단계 조금 넘어야 바뀜), 숨쉬기·끄덕임·놀람·시무룩은 머리·어깨를 정수 px로 올리고 내림(`lift`), 기쁘면 폴짝. 대화창이 떠도 제자리(사용자 요청). 자세는 **초당 최대 12번**(사용자 요청). 시간대는 방 그림만 바꿈(캐릭터 색조는 패널에서 칙칙해 보여 뺐음). 팔레트는 채도·감마 적용 후 2×2 디더 4벌 |
| `heroine_layers` | `BackdropLayer`(미리 그린 방), `PortraitLayer`(기울임 단계의 자세 + 표정 패치 2장을 행 단위로 복사, 가슴 행은 `lift`만큼 늘이거나 줄임. 자세가 바뀌면 그녀 영역, 표정이 바뀌면 패치 영역만 다시 그림), `DialogLayer`(반투명 대화창, 이름표, 타자, ▼, 하단 선택지 버튼) |
| `heroine_portrait.*` | 생성됨: 팔레트(RGB와 불투명도, 펌웨어가 채도·감마 적용), 자세 7장(`SpanImage` 384×252, 화면엔 위 240행), 단계별 표정 13장(`PortraitPatch`), 가슴 행(172), 그리는 영역. 약 390 KB |
| `heroine_room.*` | 생성됨: 시간대별 방 그림 4장(`RleImage`, 합계 약 48 KB)과 벽 색. 원본은 `art/heroine/room.svg`(창문, 하늘, 구름/별/달, 커튼, 화분) |
| `heroine_style.h`, `heroine_json` | `HeroineConfig`: name, accent, time(auto/morning/day/evening/night), textMs, sleepAfter, saturation, gamma |
| `gfx/span_image` | 행마다 빈칸을 건너뛴 팔레트 텍셀 구간. 같은 행은 여러 그림이 공유. 2×2 순서 디더 팔레트(4벌)로 그림 |
| `gfx/rle_image` | 행마다 같은 색 구간(길이, 팔레트 번호)으로 저장한 불투명 그림. 그리기는 구간 채우기뿐 |

- 프레임 이름(`kHeroineFrame*`): EyesHalf, EyesClosed, EyesSmile, LookLeft, LookRight, Surprised, Sad, Annoyed(얼굴 전체), MouthHalf, MouthOpen, MouthSmile, MouthO, MouthPout.
- 새 표정을 넣으려면: GPT 편집본을 `art/heroine/src/<이름>.png`로 두고 `rig.json`의 `frames`에 한 줄(펌웨어 이름, 편집본 이름, 바뀌는 영역 eyes/brows/mouth)을 넣은 뒤 `convert.py`를 돌립니다(스튜디오의 표정 탭에서 올리기·추가·변환까지). 펌웨어에서는 `kHeroineFrame<이름>`을 쓰는 곳만 고칩니다.
- 얼굴 좌표(`rig.json`의 `face`, 원본 1024 px 기준): 눈 (451,308)·(595,294), 눈썹 위, 입 (532,386). 굽히기 기준(`portrait.tilt`, 기기 px): 목 피벗 (192,182), 머리→몸 150~182행, 머리카락이 머리를 따라가는 건 175~205행까지, 가슴 172행(표정 패치는 이보다 위여야 함, 도구가 검사).

### 픽셀 몬스터 파일 (`src/app/ui/monster/`)

| 파일 | 역할 |
|---|---|
| `monster_sprites` | 그림을 묶는 곳. 그림 자체는 `art/monster/`의 PNG 시트이고, 빌드할 때 `monster_art.h`(생성, `PixelArt` 문자 격자)로 바뀝니다. 스타팅은 `kSpecies[kSpeciesCount]`(생성된 `kCharacters`): id, `Pose` 순서의 프레임 10장(필드 16×16 아래·위·옆·들림 × 서기·걷기, 잠, 배틀 뒷모습 32×32), 나머지 정보 JSON. 공통: 상대(날개 달린 편지) 24×24, 타일 4종, 말풍선 6종 |
| `monster_layers` | SceneLayer(풀밭 타일 / 배틀 배경·발판), SpriteLayer(팔레트 그림 + 그림자), BoxLayer(대화창·배틀 메뉴, 타자 효과, 바뀐 줄만 다시 그림), PlateLayer(이름표·HP), FxLayer(기술 파편: 네모·동그라미), WipeLayer(번쩍·줄무늬 전환) |
| `monster_character` | 상태 기계: 필드(산책·두리번·졸기·탭하면 폴짝/그 자리로 걷기) → 알림("!" → 대화창) / 승인 요청("!" → 번쩍 → 배틀 → 기술 메뉴 → 기술 연출 → 결과 → 필드). 종에 따라 그림, 불꽃 깜빡임(불꽃만), 기술 파편(불꽃·잎·물방울)이 다름 |
| `monster_style.h`, `monster_json` | `MonsterConfig`(species = `kSpecies` 번호), JSON 코덱, 스타팅 기본값 `starterConfig`(그 종 정보의 `defaults`), 기술 모양 `speciesMove`, `describe`(설정 페이지용 종 목록·프리셋), 프리셋 = 스타팅마다 하나(ember/leaf/bubble, 종 정보의 `preset`) + 불꽃 변형 3종(frost/shiny/night) |

- 팔레트 자리 `accent`/`accent2`는 종의 특징입니다: 불꽃(불꽃/불꽃 속), 풀(잎/잎 밝은 쪽), 물(등껍질/테두리).
- **새 종은 그림과 정보만으로 추가합니다(2026-10-26, 사용자 요청 "공통/캐릭터별 필요사항, 캐릭터 추가"):** 리그 스튜디오의 `monster`에서 '+ 몬스터 추가'(빈 시트, 또는 기존 몬스터의 그림 복사) → 시트를 새로 그려 올리고 정보(이름표, 기본 이름, 프리셋, 기술 모양 flame/leaf/bubble, 특징 이름, 기본 색)를 저장. 손으로 하면 `sprites.json`의 `characters.list`에 한 칸 + `<id>.png`, `<id>_back.png` + 팔레트 하나. 펌웨어는 생성된 `kCharacters`에서 종 목록을 읽고(`kSpecies`), 설정 페이지는 `/api/meta`의 `species`(id, 이름표, 이름, 프리셋, 특징 이름)로 스타팅 고르기를 그립니다. 시트 칸 순서와 `Pose`는 `monster_sprites.cpp`의 static_assert가 맞춰 봅니다. 종 하나당 플래시 약 3 KB이고 RAM은 늘지 않습니다.
- 이 구조로 바꾸면서 기존 세 마리가 그대로인지 재생 테스트로 확인: 리플레이에 `-DREPLAY_ACTIVE=1`(몬스터 대본: 세 종의 배틀 기술, 변형 프리셋, 2127프레임)을 더해, 바꾸기 전 빌드와 같은 체인 해시. 슬라임 재생도 그대로.
- 그림 고치기(2026-10-26, 사용자 요청 "캐릭터 스프라이트로 관리"): `art/monster/*.png`를 도트 편집기로 고치고 빌드하면 끝입니다. 빌드 전 훅(`tools/sprites/pio_hook.py`, 시뮬레이터는 `sim.py`)이 바뀐 시트만 `monster_art.h`로 바꿉니다. 시트 색은 팔레트 자리를 고르는 열쇠(`sprites.json`), 형식·검사·확인한 것은 ARCHITECTURE 8b.4.

- 기술 결과 문구: 첫 번째 선택지(승인)면 "효과는 굉장했다!"(상대가 쓰러짐), 나머지는 "○○는 도망쳤다!", 시간 초과는 "○○는 날아가 버렸다...".
- 표정 미리보기는 머리 위 말풍선으로 보입니다(기쁨 ♥, 놀람 !, 졸림 zZ, 화남, 슬픔 💧).

## 4. 설정 JSON

**문서 형식** (웹 `/api/config`, 시리얼 `config`, `@config`가 같음):

```text
GET  → {"character":"<활성>","<활성>":{...그 캐릭터 설정...}}
POST → {"character":"monster"} 전환 / {"slime":{...}} {"monster":{...}} 병합 / 슬라임 키를 최상위에 둔 예전 형식도 슬라임으로 병합
       모르는 캐릭터 이름이 있으면 400, 아무것도 바뀌지 않음
```

- **슬라임 키:** `size bodyShape aspect background body eye shine cheek opacity highlight cheekAlpha eyeSpacing eyeHeight gazeReach shineTravel eyeSet eyes.* brows.* accessories.* physics.* motion.*` (전체 약 1.8 KB)
- **히로인 키(`heroine`):** `name` `accent`(이름표·선택지 색) `time`(auto/morning/day/evening/night) `textMs` `sleepAfter` `saturation`(0.5~2.5, 기본 1.3) `gamma`(0.6~1.8, 기본 1.1). 채도·감마는 TN 패널에서 밝은 피부가 회색으로 바래 보이는 걸 보정합니다(팔레트에만 적용, 비용 없음). 프리셋 없음.
- **Capri 키(`capri`):** `call`(부르는 이름, 비우면 촘/choom) `lang`(ko/en) `accent`(태그 없는 카드·혼잣말 테두리) `textMs` `sleepAfter` `chatter`(먼저 말 거는 간격 분, 0 = 끔) `glitch`(0~100) `cutins` `sound` `volume` `brain`(길게 누르면 brain에게). 카드 JSON에 `"tag"`, `"face"`(ARCHITECTURE 8d).
- **픽셀 몬스터 키(`monster`):** `species`(fire/grass/water) `name`(UTF-8, 한글 8자) `body belly accent accent2 scarf grass`(색) `scale`(2~4) `walkSpeed`(그림 픽셀/초) `textMs`(글자당 ms) `sleepAfter`(초) `level`. `species`만 바꾸면 그림만 바뀌고, 프리셋 `ember`/`leaf`/`bubble`은 그 스타팅의 이름과 색까지 바꿉니다.
- 색은 `"#rrggbb"`이고 저장 시 RGB565로 양자화됩니다.
- **NVS** (`cyd` 네임스페이스): 캐릭터마다 키 하나(`slime`, `monster`)와 `active`, 터치·롱 프레스·드래그 보정 `touchcal`, 집 Wi-Fi·MQTT `net`(`{"wifi":{"ssid","pass"},"mqtt":{"host","port","tls","user","pass","topic"}}`, 비밀번호 평문).
- **네트워크 설정 문서**(`/api/net`, 시리얼 `net`, `@net`): GET은 위 키에서 비밀번호 대신 `passSet`, 그리고 상태 `status`(off/connecting/connected)·`error`·`ip`·`rssi`. POST는 일부만 보내도 되고(빠진 키는 그대로, `"topic":""`은 기본값), 바로 저장·재연결. 너무 긴 값이나 `+ #`·끝 `/`가 있는 토픽은 400.
  - 슬라임 태그가 쓰던 `character` 키(평면 슬라임 JSON)는 부팅 때 슬라임으로 읽고, 첫 저장 때 지웁니다.
  - 키 하나당 NVS 문자열 한도(약 4000 B) 안에 들어가야 합니다.
- **웹 API** (기기 AP `CYD-Blob-3294` / 비밀번호 `blob1234` / http://192.168.4.1, 또는 집 Wi-Fi의 주소. AP와 웹 서버는 필요할 때만 켜짐, ARCHITECTURE 9.3):
  - `GET /`, `/api/meta`(`{"expressions":[..],"characters":[{"name":..,프리셋·선택 목록}]}`), `/api/config`
  - `POST /api/config`, `/api/touchcal`(기기에서 설정 메뉴를 엶), `/api/save`(전부 저장), `/api/reset`(활성 캐릭터만 기본값), `/api/preset`(활성 캐릭터), `/api/preview`, `/api/card`(`"sticky":true` 가능), `/api/time`(`{"epoch":초,"offset":분}`), `GET/POST /api/net`
- **시리얼 명령:** `n 제목|내용`, `p 제목|내용|선택1|선택2`, `char <이름>`, `ask <말>`(Capri의 brain에게, MQTT), `preset <이름>`, `time HH:MM`, `face <표정>`, `config [json]`, `net [json]`, `save`, `reset`, `stress`, `bench`, `menu`(설정 메뉴, `touchcal`도 같음), `web [on|off]`, `sleep`, 슬라임 전용 `shape` `brows` `eyes` `acc`, 기계용 `@meta @config @merge @save @reset @preset @preview @card @time @net`
- **카드 글자 한도(바이트, UTF-8):** 제목 48, 본문 192, 선택지 24 = 한글 16 / 64 / 8자.

## 5. 테스트 방법과 함정

- **화면은 먼저 시뮬레이터로:** `python tools/web/sim.py` → http://localhost:8766
  - `src/app/`, 웹 보드·플랫폼을 Emscripten으로 빌드합니다. 펌웨어와 소스가 같아서 그려지는 픽셀, 물리, 프레임 주기가 같습니다.
  - 왼쪽은 화면(마우스가 터치), 오른쪽은 기기의 설정 페이지입니다. 저장은 localStorage에 하고, 키 구조는 NVS와 같습니다.
  - 소스를 저장하면 다시 빌드하고 새로 고칩니다. 시계는 브라우저 시간을 씁니다.
  - Claude는 브라우저 패널(launch 설정 `sim`)에서 스크린샷으로 확인할 수 있습니다.
  - 스크린샷이 느려서 빠른 연출은 놓치기 쉽습니다. 페이지 JS로 정해진 시각마다 캔버스를 복사해 한 장에 모아 보면 됩니다.
  - **같지 않은 것:** 성능, 실제 패널 색, 감압식 터치, Wi-Fi와 NVS, Xtensa GCC 전용 문제. 이건 기기에서 확인합니다.
- **슬라임이 그대로인지(공유 코드를 고쳤을 때마다):** 재생 테스트 `env replay`
  1. 이 트리: `python -m platformio run -e replay -t upload` → `python tools/serial/bootlog.py COM3 45 > new.txt`
  2. 태그: `git archive slime`을 임시 폴더에 풀고, `src/tools/replay/`와 `platformio.ini`의 `[env:replay]`를 복사한 뒤 같은 방법으로 빌드·캡처 → `tag.txt`
  3. 두 파일에서 `t=`, `done`, `config` 줄을 비교합니다. 2049프레임 화면 해시, 픽셀 스트림, 설정 JSON 해시, 탭 판정이 모두 같아야 합니다.
  - 기준값: `done frames 2049 chain 44531f9c px 30815819 stream 6449ed96`
  - 하네스는 `__has_include("app/ui/characters.h")`로 태그 API와 새 API를 둘 다 빌드합니다.
  - 재생 중에는 보드 화면이 꺼져 있습니다. 끝나면 cyd2usb를 다시 올리세요.
  - CYD가 없으면 ESP32-S3 보드로 env `replay_s3`(출력은 그 보드의 네이티브 USB 포트). USB가 USB-UART 칩을 거치는 S3 보드(SpotPear)는 `replay_s3_uart`(UART0 출력, `bootlog.py`로 읽음). 태그 트리에도 같은 env를 넣어 **같은 칩끼리** 비교합니다. S3에서도 기준값이 같게 나옴(2026-10-22).
  - **몬스터 재생(2026-10-26):** `-DREPLAY_ACTIVE=1`로 빌드하면(`$env:PLATFORMIO_BUILD_FLAGS="-DREPLAY_ACTIVE=1"`) 몬스터 대본을 돌립니다: 알림, 세 종의 배틀(원격 답처럼 `Answer`로 기술), 변형 프리셋, 2127프레임. SpotPear 기준 `done frames 2127 chain 8b508ea7 px 34210918 stream 155fea9e`. 몬스터 코드를 바꿀 때 같은 방식으로 비교합니다.
- **올리고 수치 확인:**
  1. `python -m platformio run -e cyd2usb -t upload`
  2. `python tools/serial/serial_cmd.py COM3 20 "wait:8" "p Deploy?|Release?|Approve|Deny"` → 2초마다 fps, 그리기/SPI, CPU, 힙
- **MQTT 시험:**
  1. PC에서 `python tools/mqtt/blob_mqtt.py broker`(0.0.0.0:1883, 처음엔 Windows 방화벽이 python 허용을 물음. 이 PC는 python.exe 차단 규칙이 있었음). PC만 쓰는 자체 시험은 `--bind 127.0.0.1`.
  2. 기기 설정 페이지 "네트워크"에 집 Wi-Fi(사용자가 비밀번호 입력)와 브로커 주소 = PC IP(브로커가 시작할 때 출력).
  3. `blob_mqtt.py notify "제목|내용"`, `prompt "제목|내용|승인|거절"`(응답을 기다려 출력), `watch`. 기기는 시리얼에 `mqtt: connected`, `mqtt: card queued: ...`를 찍음.
  - 브로커 없이 도구만 시험: 시험 브로커를 `--bind 127.0.0.1 --port 18883`로 띄우고 펌웨어처럼 행동하는 클라이언트(last will, retained status, 승인 요청에 0번 응답)를 붙여 notify/prompt/time을 돌리면 됨(2026-10-23에 이렇게 확인).
- **기기 화면은 Claude가 볼 수 없습니다.** 확인한 것과 확인하지 못한 것을 나눠 말하고, 기기 확인은 사용자에게 부탁하세요.
- **성능 기준 (실측, AP와 웹 서버 켬):**

| 상황 | 그린 fps | 그리기 + SPI | px/프레임 | 코어1 |
|---|---|---|---|---|
| 슬라임 대기 | 27~30 | 4.4 + 2.6 ms | 12.4K | 18~20% |
| 슬라임 이동 | 50~60 (1초 이내) | 4.5~7.5 ms | 14~18K | 40~56% |
| 픽셀 몬스터 필드 대기·산책 | 8~22 | 0.5 + 0.3 ms | 2.5K | 1~4% |
| 픽셀 몬스터 배틀 전환 | 15 | 5.0 + 15.1 ms | 48K | 2~11% |
| 픽셀 몬스터 기술 메뉴 대기 | 8 | 1.9 + 2.5 ms | 8K | 3% |

  - "그린 fps"는 실제로 바뀐 프레임 수입니다. 픽셀 몬스터는 움직임이 없으면 그리지 않습니다.
  - 여유 힙: 약 151 KB(최저 142 KB). 태그 때 163 KB에서 줄어든 것은 카드 버퍼(한글) 약 6 KB, USB 브리지 RX 버퍼 2 KB, 픽셀 몬스터 약 3 KB 때문입니다.
  - 캐릭터는 모두 메모리에 상주합니다(슬라임 약 12 KB, 픽셀 몬스터 약 3 KB, 레이어 14/16개). 캐릭터를 더 넣다 RAM이나 레이어가 모자라면, 활성 캐릭터만 만드는 방식으로 `CharacterSet`을 바꾸세요.
  - 플래시: 1.14 MB / 1.9 MB(58%). 한글 폰트가 약 75 KB입니다.
  - 그리기 시간은 코드 배치(플래시 캐시)에 따라 빌드마다 ±4% 흔들립니다. 그리기 코드를 안 바꿔도 그렇습니다.
- **함정:**
  - QMI8658 저역 필터는 출력 속도의 비율로 잘림: 125 Hz의 2.66%(약 3 Hz)면 손으로 흔든 게 칩 안에서 사라짐. 250 Hz + 13.37%(약 33 Hz)로 둠.
  - **ESP32-S3의 GPIO39~42는 켜질 때 JTAG 기능**입니다. `gpio_set_direction`만으로는 GPIO로 바뀌지 않아 핀이 안 움직입니다. `gpio_config`(또는 `pinMode`)를 쓰세요(2026-10-26, SpotPear 보드의 LCD 리셋 핀에서 화면이 까맸던 원인).
  - 다른 파일의 전역 생성자가 읽는 표는 `constexpr`이어야 합니다(그림 `PixelArt`, 폰트 표도 constexpr).
  - Xtensa GCC 내부 컴파일러 오류가 나면 거리 함수 분기를 `noinline` 함수 하나로 모으세요.
  - `releases/*.bin` 이미지는 NVS까지 덮어씁니다.
  - `PERF_PROBE=1`은 idle 태스크를 깨워두는 개발용 설정입니다.
  - 시리얼 포트를 그냥 열면 보드가 리셋됩니다. `serial_cmd.py`는 DTR/RTS를 내려서 리셋을 피합니다.
  - PowerShell 5.1은 네이티브 프로그램 인자의 큰따옴표를 지웁니다. JSON을 시리얼로 보낼 때는 Bash 도구를 쓰세요.
  - 이 세션 환경은 계산된 값이 들어간 셸 명령이나 heredoc을 거부할 때가 있습니다. 스크립트는 파일로 만들어 실행하세요.
  - 한글 폰트는 KS X 1001 2,350자뿐입니다. 없는 글자는 □로 나옵니다.
  - **이 TN 패널은 모니터에서 안 보이는 것을 보여 줍니다.** 패치 사각형 경계의 1~6단계 색 단차(직선이라 "안경 테두리"처럼 보임), 캐릭터 전체에 섞은 색조(연한 피부만 죽어 보임). 프레임은 기기 데이터(양자화된 픽셀)에서 경계가 원본과 같은지 확인하세요(`portrait.py`의 settle).
  - 편집본의 반투명 섞기는 원본 눈의 잔상을 남깁니다. 눈·입 영역은 편집본으로 통째로 바꾸고, 바깥 피부만 부드럽게 잇습니다(`frames.py`).
  - Xtensa GCC 내부 컴파일러 오류는 다각형 래스터라이저(지금은 지움)의 반복문 안 float 계산에서도 났습니다. 같은 증상이면 계산을 `noinline` 함수로 빼세요.
  - Git Bash heredoc이 Python 문자열의 `\n`을 바꿔 버릴 때가 있습니다. 패치 스크립트는 Write 도구로 파일을 만들어 실행하세요.
  - 이 PC는 Intel 내장 그래픽뿐이라 CUDA가 없습니다. 그림 도구는 numpy/OpenCV(설치됨)로 씁니다.
  - **기기에서 그림을 굽히지 마세요.** 삼각형 986개 쌍선형 메시는 고정소수점으로 다듬어도 다시 그릴 때마다 약 45 ms(코어1 60~100%)였고, 삼각형 이음새가 회색 선으로 보였습니다. 자세는 `portrait.py`에서 미리 그립니다(사용자 제안).
  - **매끈한 피부는 565와 팔레트 단계에서 회색 등고선이 됩니다**(눈 밑 볼, 목·쇄골). 시뮬레이터에도 보이지만 패널에서 훨씬 눈에 띕니다. 찾을 때는 생성된 표를 기기 색 계산대로 풀어서 채도 하이패스 지도를 보세요(사용자가 회색 부분과 겹친다고 확인). 디더는 순서 디더만: 섞음 노이즈(interleaved gradient)는 패널에서 대각선 줄로 보였습니다.
  - Windows에서 Python `open(p, 'w')`는 줄 끝을 CRLF로 씁니다(저장소는 LF). 패치 스크립트는 `newline='\n'`을 주세요.

## 6. 다음 작업

0. MQTT 마무리: (가) 사용자가 집 Wi-Fi를 넣고 시험 브로커(또는 사용자 브로커)로 카드·응답 왕복 확인, (나) 연결 상태와 TLS 연결 상태의 힙·CPU를 CYD와 S3에서 재기(예산: TLS 포함 힙 ≥ 100 KB), (다) CYD 플래시 98.3% 대책 결정(위 1장), (라) NTP: 집 Wi-Fi가 붙으면 SNTP로 시계(시간대는 설정 페이지가 주는 offset을 저장해 쓰면 됨).
1. 사용자에게 기기에서 확인받을 것: 기울임 7단계가 튀어 보이는지(튀면 단계를 촘촘히: 한 단계 약 46 KB), 곁눈질 눈이 튀는지, 피부 디더가 거친지(`portrait.py`의 `DITHER`).
2. 태그 `monster`와 `anime`는 돌아올 지점일 뿐 슬라임처럼 고정한 것은 아닙니다(재생 테스트는 슬라임만 봄). 바로 올리는 이미지가 필요하면 `releases/<태그>/`를 슬라임과 같은 방식으로 만듭니다.
3. 사용자가 원한 다음 단계: "웬만한 동작"을 더 만드는 것. 이제는 PC에서 자세를 더 굽혀 넣는 방식입니다(플래시 여유 약 330 KB, 자세 한 장 약 46 KB). 후보: 머리카락 흔들림 단계, 고개 좌우 돌리기, 숙이기. 부족하면 사용자는 캐릭터 해상도를 조금 낮춰도 된다고 했습니다.
4. 피쳐폰 "사진 움직이기" 방식의 새 캐릭터: 폰으로 사진을 올리고 눈·입을 찍는 방식. 기기에서 메시로 굽히기에는 이 보드가 느려서(위 함정), 자세를 미리 그릴 곳(PC나 웹 페이지)이 필요합니다.
5. 곁눈질 두 장(LookLeft/Right)은 작은 패널에서 키워 조금 흐립니다. 원본 크기로 다시 받으면 교체합니다.
6. 사용자가 "카드 추가"(새 카드 종류나 화면)를 고민 중입니다. 정해지면 엔진 `CardKind`, 슬라임 `CardLayer`, 몬스터 연출, 애니 대화창에 각각 넣어야 합니다.
7. 픽셀 몬스터를 다듬을 때 후보로 생각해 둔 것:
   - 상대 그림을 요청 종류에 따라 바꾸기
   - 필드 장식(나무, 울타리)
   - 걷기 프레임 늘리기
   - 배틀 전환 시간 줄이기(지금 "!" 0.45초 + 전환 1초 + 인트로 문구 약 1.3초)

## 7. 남은 로드맵

1. ~~집 Wi-Fi 접속 설정(STA)~~(됨, mDNS는 뺌: ARCHITECTURE 9.3). NTP로 시계 자동 맞춤
2. ~~서버 프로토콜과 테스트 서버~~(MQTT와 `tools/mqtt`, 기기에서 브로커 연결 확인은 남음)
3. SD 카드와 알림음
4. 절전: 조도 센서 밝기, 화면 끄기, PERF_PROBE 끄기, 배터리
5. ~~외부 캐릭터 변환기(그림 → 엔진 데이터)~~ (됨: 픽셀은 PNG 시트 → `tools/sprites`, 빌드 때 자동. 2D는 그림 + `rig.json` 세트 → `tools/rig/convert.py`. 슬라임은 그림이 아니라 JSON 설정)
6. SPI 80 MHz 시험
