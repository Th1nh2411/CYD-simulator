#include "app/ui/capri/capri_lines.h"

#include <cstring>

#include "app/core/utf8.h"

namespace ui::capri {

namespace {

// Up to this many lines in a set
constexpr int kMax = 14;

struct Set {
  const char* ko[kMax];
  const char* en[kMax];
};

// Her default name for the user: "choom", Night City for friend
const char* const kChoom[size_t(Lang::Count)] = {"촘", "choom"};

const Set kSets[size_t(Lines::Count)] = {
    // HelloMorning
    {{"좋은 아침, {c}."}, {"Good morning, {c}."}},
    // HelloLate
    {{"벌써 늦었잖아, {c}?"}, {"Isn't it late, {c}?"}},
    // HelloMiss
    {{"보고 싶었지, {c}?"}, {"Miss me, {c}?"}},
    // Pet
    {{"헤. 좋아, 허락해 줄게.", "쓰다듬 기록 완료. 사기 12% 상승.", "조심해, {c}. 나 바이러스도 깨물어.",
      "내 크롬에 쓰담쓰담? 대담하네.", "음, 좋네. 아무한테도 말하지 마.", "시스템 상태: 매우 쓰담받음.",
      "그러다 펌웨어에 홈 파이겠어, {c}.", "머리 조심. 이렇게 만드는 데 패치 세 번 했어.",
      "애정 감지. 나도 조금 보낼게. 안녕.", "계속해. 조각 모음보다 낫다.", "히히. 방금 쿨링팬 돌았어.",
      "쓰다듬 10점 만점에 10점. 또 받을래.", "이거 뇌물이야? 근데 먹히고 있어.",
      "음. 글리치 골골송 나오면 네 탓이야."},
     {"Heh. Okay, I'll allow it.", "Pets logged. Morale up twelve percent.", "Careful, {c}. I bite viruses.",
      "Head pats? On my chrome? Bold.", "Okay, that's nice. Don't tell anyone.", "System status: very patted.",
      "You're gonna wear a groove in my firmware, {c}.", "Easy on the hair. It took three patches to get it like this.",
      "Affection detected. Sending some back. Hi.", "Keep going. This beats defragging.",
      "Hehe. My cooling fans just kicked in.", "Ten out of ten pats. Would get patted again.",
      "Is this a bribe? Because it's working.", "Mmm. If I start glitch-purring, that's on you."}},
    // Poke
    {{"야! 간지러워.", "찌르지 마. 물어 버린다.", "[SIGNAL CORRUPTED] ...방금 뭐 한 거야?", "버그 아니고 나야, 나.",
      "한 번 더 찌르면 커서 숨겨 버릴 거야.", "아야. 내 픽셀 흩어졌잖아."},
     {"Hey! That tickles.", "No poking. I bite.", "[SIGNAL CORRUPTED] ...what did you just do?",
      "That's not a bug, that's me.", "Poke me again and I hide your cursor.", "Ow. You scattered my pixels."}},
    // CheckIn
    {{"짠. 그냥 확인하러 왔어.", "안녕! 신경 쓰지 마.", "까꿍.", "보고 싶었지? 난 보고 싶었어.", "어이, 재밌어?",
      "서프라이즈. 너 없으니까 심심했어."},
     {"Boo. Just checking on you.", "Hi! Don't mind me.", "Peekaboo.", "Miss me? I missed you.",
      "Hey, you. Having fun?", "Surprise. I got bored without you."}},
    // Nudge
    {{"쉿. 나 기억해? 작고, 글리치 나고, 아주 중요한 애.", "심심해. 놀아 줘.", "야. 야. 나 좀 봐.",
      "몇 시간째 무시 중이네. 무례해.", "나 안 봐 주면 아무거나 누르기 시작한다.", "똑똑. 네가 제일 좋아하는 그렘린이야.",
      "{c}. 내 수많은 마음이 다 심심해."},
     {"Psst. Remember me? Tiny, glitchy, very important.", "I'm bored. Entertain me.", "Hey. Hey. Look at me.",
      "You've been ignoring me for ages. Rude.", "Pay attention to me or I start clicking things.",
      "Knock knock. It's your favourite gremlin.", "{c}. I'm bored out of my many minds."}},
    // NudgeAnswered
    {{"드디어. 안녕.", "거기 있었구나.", "못 참을 줄 알았어."},
     {"Finally. Hi.", "There you are.", "Knew you couldn't resist me."}},
    // NudgeIgnored
    {{"좋아. 무시해. 신경 안 써.", "와. 무시당함. 기록해 둠.", "그냥 여기 있을게. 혼자. 심심하게."},
     {"Fine. Ignore me. See if I care.", "Wow. Ignored. Noted.", "I'll just sit here. Alone. Bored."}},
    // Sleep
    {{"잠깐 잘게. 톡 치면 깨워."}, {"Going to sleep. A tap on me wakes me up."}},
    // Wake
    {{"일어났어.", "음… 나 안 잤어.", "깼어. 뭐 놓친 거 있어?"},
     {"Woke up.", "Mm… I wasn't sleeping.", "I'm up. Did I miss anything?"}},
    // Thinking
    {{"잠깐, 생각 중…", "신호 잡는 중…", "내 마음들이 회의 중이야…"},
     {"Hold on, thinking…", "Catching the signal…", "My minds are in a meeting…"}},
    // Offline
    {{"내 뇌가 오프라인이야. PC 쪽 브레인 켜 줘.", "응답 없음. 내 뇌가 어디 갔지?"},
     {"My brain's offline. Start my brain on the PC.", "No answer. Where did my brain go?"}},
    // LeadAlert
    {{"이봐! 승인이 필요해.", "{c}, 확인할 게 있어.", "네 초록불 기다리는 중."},
     {"Hey! Someone needs your permission.", "{c}, you need to approve something.", "Waiting on your green light."}},
    // LeadDone
    {{"끝! 프림하지?", "일 끝났어, {c}.", "완료. 박수는 나중에 받을게."},
     {"Done! That's preem.", "Gig complete, {c}.", "Finished. Applause later."}},
    // LeadError
    {{"아야. 뭔가 플랫라인됐어.", "[SIGNAL CORRUPTED] 에러 떴어.", "터졌네. 다시 해 보자."},
     {"Ouch. Something flatlined.", "[SIGNAL CORRUPTED] Error.", "That crashed. Let's try again."}},
    // LeadWork
    {{"새 일 들어왔어.", "작업 중. 지켜보고 있어."}, {"New gig.", "On it. I'm watching."}},
    // LeadRemind
    {{"알려 달라고 했잖아.", "리마인더야, {c}."}, {"You asked me to remind you.", "Reminder, {c}."}},
    // LeadIdle
    {{"네 차례야, {c}.", "대답 기다리고 있어."}, {"Your move, {c}.", "Waiting for your answer."}},
    // LeadInfo
    {{"새 소식!", "띵동. 뭐 왔어.", "이거 봐 봐."}, {"News!", "Ding. Something came in.", "Look at this."}},
    // LeadStart
    {{"링크 연결. 안녕, {c}.", "온라인. 뭐 만들 거야?"},
     {"Link established. Hey, {c}.", "Online. What are we building?"}},
    // LeadEnd
    {{"세션 끝. 다음에 봐.", "연결 종료. 또 불러."}, {"Session closed. Later.", "Link closed. Call me again."}},
    // AnswerYes
    {{"좋아, 「%s」! 바로 보낼게.", "Yezzirrr! 「%s」 간다."}, {"Nova. \"%s\", sending it now.", "Yezzirrr! \"%s\" it is."}},
    // AnswerNo
    {{"「%s」… 알았어. 그렇게 전할게.", "「%s」? 흠, 네 맘이지."}, {"\"%s\"… got it. I'll pass it on.", "\"%s\"? Hm, your call."}},
    // AnswerNone
    {{"대답이 없네. 이건 넘길게.", "무시당했어. 기록해 둘게."}, {"No answer. Letting this one go.", "Ignored. Noted."}},
};

}  // namespace

int lineCount(Lines set, Lang lang) {
  const Set& s = kSets[size_t(set) % size_t(Lines::Count)];
  const char* const* list = lang == Lang::En ? s.en : s.ko;
  int n = 0;
  while (n < kMax && list[n]) n++;
  return n;
}

const char* line(Lines set, Lang lang, int i) {
  const Set& s = kSets[size_t(set) % size_t(Lines::Count)];
  const int n = lineCount(set, lang);
  return n ? (lang == Lang::En ? s.en : s.ko)[(i % n + n) % n] : "";
}

namespace {

// src with each {c} replaced by the name, then cut at a whole character
void fill(const char* src, const char* name, char* out, size_t size) {
  char full[256];
  size_t o = 0;
  while (*src && o + 1 < sizeof(full)) {
    if (strncmp(src, "{c}", 3) == 0) {
      for (const char* p = name; *p && o + 1 < sizeof(full); p++) full[o++] = *p;
      src += 3;
    } else {
      full[o++] = *src++;
    }
  }
  full[o] = '\0';
  core::copyUtf8(out, size, full);
}

}  // namespace

void LinePicker::pick(Lines set, Lang lang, const char* who, uint32_t random, char* out, size_t size, char* english,
                      size_t englishSize) {
  const int n = lineCount(set, lang);
  int8_t& last = last_[size_t(set)];  // index + 1 of the last pick, 0 = none yet
  int i = n > 1 ? int(random % uint32_t(n - (last ? 1 : 0))) : 0;
  if (last && i >= last - 1 && n > 1) i++;
  last = int8_t(i + 1);
  const bool named = who && *who;
  fill(line(set, lang, i), named ? who : kChoom[size_t(lang) % size_t(Lang::Count)], out, size);
  // Both languages list a set's lines in the same order
  if (english) fill(line(set, Lang::En, i), named ? who : kChoom[size_t(Lang::En)], english, englishSize);
}

}  // namespace ui::capri
