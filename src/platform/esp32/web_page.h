#pragma once

// Character settings page, served at /. Talks to /api/* (see web_server.cpp).
// Controls are generated from the schema tables at the top of the script.
inline constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blob Studio</title>
<style>
:root{--bg:#0e101e;--panel:#181b30;--line:#2c3052;--text:#e8e8f0;--dim:#9a9cc0;--accent:#6ec8ff}
*{box-sizing:border-box}
body{margin:0;font:14px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;z-index:2;background:var(--bg);padding:10px 16px;border-bottom:1px solid var(--line);display:flex;gap:8px;align-items:center}
header h1{font-size:16px;margin:0;flex:1}
main{padding:12px 16px 40px;max-width:720px;margin:auto}
section{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:12px;margin-bottom:12px}
h2{font-size:12px;margin:0 0 8px;color:var(--dim);letter-spacing:.05em}
.row{display:grid;grid-template-columns:120px 1fr 48px;gap:8px;align-items:center;margin:4px 0}
.row output{text-align:right;color:var(--dim);font-variant-numeric:tabular-nums;font-size:12px}
input[type=range]{width:100%}
input[type=color]{width:100%;height:30px;border:0;padding:0;background:none}
input[type=text],input[type=password],input[type=number]{width:100%;background:#262a4a;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:6px 8px;font:inherit}
summary{cursor:pointer;color:var(--dim);font-size:12px;margin:4px 0}
.hint{color:var(--dim);font-size:12px;margin:6px 0;word-break:break-all}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px}
button{background:#262a4a;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:7px 11px;font:inherit;cursor:pointer}
button.on,button.primary{background:var(--accent);color:#0e101e;border-color:var(--accent)}
#status{color:var(--dim);font-size:12px}
select{background:#262a4a;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:6px;font:inherit}
</style></head><body>
<header><h1>Blob Studio</h1><span id="status"></span>
<select id="uiLang" title="Language"><option value="ko">한국어</option><option value="en">English</option></select>
<button id="clock">시계 맞추기</button><button id="reset">초기화</button><button class="primary" id="save">저장</button></header>
<main>
<section><h2>캐릭터 (누르면 바로 바뀜, 저장하면 다음 부팅에도 유지)</h2><div class="chips" id="chars"></div></section>
<section><h2>프리셋 (적용 후 수정·저장 가능)</h2><div class="chips" id="presets"></div></section>
<section><h2>미리보기</h2><div class="chips" id="exprs"></div>
<div class="chips"><button id="tNotify">알림 테스트</button><button id="tPrompt">승인 요청 테스트</button><button id="tTouch">터치 보정</button></div></section>
<section id="disp" style="display:none"><h2>화면 방향 (바꾸면 기기가 다시 시작함)</h2><div class="chips" id="rotation"></div>
<div class="chips"><button id="mirror">좌우 반전 (프리즘)</button></div></section>
<section id="net" style="display:none"><h2>네트워크 (집 Wi-Fi와 MQTT, 적용하면 바로 저장)</h2><div class="hint" id="netState"></div>
<div class="row" id="otpRow" style="display:none"><label>AI Buddy OTP</label><input id="otpCode" maxlength=6 inputmode=numeric placeholder="백오피스의 6자리"><button id="otpSend">보내기</button></div>
<details><summary>설정 열기</summary><div id="netForm"></div>
<div class="hint" id="netTopics"></div><div class="chips"><button class="primary" id="netApply">적용</button></div></details></section>
<div id="ed-slime">
<section><h2>색상</h2><div id="colors"></div></section>
<section><h2>몸 (머리) 형태</h2><div class="chips" id="shapes"></div><div id="body"></div></section>
<section><h2>눈썹</h2><div class="chips" id="browStyles"></div><div id="brows"></div></section>
<section><h2>눈 세트</h2><div class="chips" id="sets"></div><div id="eyeLayout"></div></section>
<section><h2>표정별 눈 모양 (편집 중인 표정이 화면에 미리 보임)</h2><div class="chips" id="tabs"></div><div id="eyeShape"></div></section>
<section><h2>꾸미기</h2><div id="acc"></div></section>
<section><h2>움직임</h2><div id="motion"></div></section>
<section><h2>유체 물리</h2><div class="chips" id="physPresets"></div><div id="physics"></div></section>
</div>
<div id="ed-heroine">
<section><h2>이름 (대화창 이름표, 한글 8자까지)</h2><div id="heroName"></div></section>
<section><h2>포인트 색 (이름표·선택지·화살표)</h2><div id="heroColors"></div></section>
<section><h2>방 시간대 (자동이면 시계를 따름)</h2><div class="chips" id="heroTime"></div></section>
<section><h2>패널 색 보정 (기기 화면을 보며 맞추기)</h2><div id="heroPanel"></div></section>
<section><h2>움직임</h2><div id="heroMotion"></div></section>
</div>
<div id="ed-capri">
<section><h2>Capri에게 말 걸기 (PC의 brain이 대답: tools/capri, MQTT 필요)</h2><div class="row"><input type=text id="capSay" maxlength=120 placeholder="오늘 뭐 했어?"><button id="capSend">보내기</button></div></section>
<section><h2>부르는 이름 (비우면 촘 / choom)</h2><div id="capCall"></div></section>
<section><h2>언어</h2><div class="chips" id="capLang"></div></section>
<section><h2>기본 테두리 색 (태그 없는 알림, 혼잣말)</h2><div id="capColors"></div></section>
<section><h2>켜고 끄기</h2><div class="chips" id="capToggles"></div></section>
<section><h2>움직임과 말</h2><div id="capMotion"></div></section>
</div>
<div id="ed-monster">
<section><h2>스타팅 몬스터 (고르면 그 몬스터의 기본 이름과 색으로 바뀜)</h2><div class="chips" id="monStarter"></div></section>
<section><h2>이름 (배틀과 메시지에 나옴, 한글 8자까지)</h2><div id="monName"></div></section>
<section><h2>색상</h2><div id="monColors"></div></section>
<section><h2>크기와 속도</h2><div id="monMotion"></div></section>
</div>
</main>
<script>
// The page is written in Korean; English comes from this table, applied to whatever
// text the page shows (an observer catches what the script draws later). Longer phrases are also
// replaced inside composed lines (network state); one-letter words only when they stand alone.
const I18N={"Capri에게 말 걸기 (PC의 brain이 대답: tools/capri, MQTT 필요)":"Talk to Capri (her brain on the PC answers: tools/capri, needs MQTT)","AI Buddy 기기 번호 (예: HB-0006, 비우면 시험용 토픽)":"AI Buddy device number (e.g. HB-0006; empty = test topics)","스타팅 몬스터 (고르면 그 몬스터의 기본 이름과 색으로 바뀜)":"Starter monster (picking one sets its default name and colors)","네트워크 (집 Wi-Fi와 MQTT, 적용하면 바로 저장)":"Network (home Wi-Fi and MQTT, saved when applied)","캐릭터 (누르면 바로 바뀜, 저장하면 다음 부팅에도 유지)":"Character (switches at once; save to keep it after a restart)","표정별 눈 모양 (편집 중인 표정이 화면에 미리 보임)":"Eyes per expression (the one being edited shows on the screen)","빌드가 끝났어요. 테스트가 모두 통과했습니다.":"The build finished. All tests passed.","이름 (배틀과 메시지에 나옴, 한글 8자까지)":"Name (in battles and messages, up to 8 Hangul)","OTP를 받을 로그인이 없음 (6자리 숫자)":"No login is waiting for an OTP (6 digits)","기본 테두리 색 (태그 없는 알림, 혼잣말)":"Default border color (untagged cards, her own lines)","패널 색 보정 (기기 화면을 보며 맞추기)":"Panel color correction (tune while looking at the device)","부르는 이름 (비우면 촘 / choom)":"What she calls you (empty = choom)","화면 방향 (바꾸면 기기가 다시 시작함)":"Screen orientation (the device restarts when changed)","이름 (대화창 이름표, 한글 8자까지)":"Name (on the dialog's name plate, up to 8 Hangul)","v1.2를 운영 서버에 배포할까요?":"Deploy v1.2 to production?","길게 누르면 brain에게 말 걸기":"Long press talks to her brain","방 시간대 (자동이면 시계를 따름)":"Room time of day (auto follows the clock)","포인트 색 (이름표·선택지·화살표)":"Accent color (name plate, choices, arrow)","프리셋 (적용 후 수정·저장 가능)":"Presets (edit and save after applying)","감마 (클수록 밝은 톤이 진해짐)":"Gamma (higher darkens the light tones)","먼저 말 거는 간격(분, 0=끔)":"Pops in every (min, 0 = never)","물방울 떨어짐(실험, 0/1)":"Dripping (experimental, 0/1)","소리 (스피커 있는 보드)":"Sound (boards with a speaker)","저장됨 (바꿀 때만 입력)":"Saved (type only to change it)","AI Buddy 비밀키":"AI Buddy secret key","비밀번호나 계정이 틀림":"Wrong password or account","화면 방향 바꾸기 실패":"Could not change the orientation","Wi-Fi를 못 찾음":"Wi-Fi not found","브로커가 응답 안 함":"The broker does not answer","좌우 반전 (프리즘)":"Mirror left-right (prism)","TLS 인증서 오류":"TLS certificate error","Wi-Fi 비밀번호":"Wi-Fi password","네트워크 설정 오류":"Network settings error","다시 시작하는 중…":"Restarting…","슬라임 (물 같은)":"Slime (watery)","초기값 (저장 전)":"Defaults (not saved)","글자 속도(ms)":"Text speed (ms)","몸 (머리) 형태":"Body (head) shape","백오피스의 6자리":"6 digits from the backoffice","승인 요청 테스트":"Test prompt","이동 시간(ms)":"Glide time (ms)","Wi-Fi 이름":"Wi-Fi name","가로 (뒤집기)":"Landscape (flipped)","가로/세로 비율":"Width/height ratio","네트워크 저장됨":"Network saved","도착 충격(텅)":"Landing thud","말랑이 (끈적)":"Squishy (sticky)","세로 (뒤집기)":"Portrait (flipped)","오늘 뭐 했어?":"What did you do today?","잠들기까지(초)":"Sleeps after (s)","주소를 못 찾음":"Address not found","네트워크 오류":"Network error","등 (몸 뒤)":"Back (behind the body)","불러오기 실패":"Loading failed","브로커가 거절":"The broker refused","졸기까지(초)":"Dozes after (s)","집 Wi-Fi":"Home Wi-Fi","클로즈업 컷인":"Close-up cut-ins","영어 목소리 (brain의 /tts)":"English voice (her brain's /tts)","OTP 보냄":"OTP sent","OTP 필요":"OTP needed","글리치 빈도":"Glitch frequency","꼬리 늘어남":"Tail stretch","눈과의 간격":"Gap to the eyes","눈꺼풀 각도":"Eyelid angle","브로커 주소":"Broker address","시계 맞추기":"Set the clock","알림 테스트":"Test notification","움직임과 말":"Motion and talk","위치(각도)":"Position (angle)","크기와 속도":"Size and speed","픽셀 몬스터":"Pixel monster","걷는 속도":"Walking speed","고양이 귀":"Cat ears","굵은 일자":"Thick line","꼬마 악마":"Little devil","눈썹 각도":"Brow angle","눈썹 높이":"Brow height","둥근 사각":"Rounded square","로그인 중":"Logging in","배포 요청":"Deploy request","부피 유지":"Volume keeping","새 메시지":"New message","설정 열기":"Open settings","소리 크기":"Volume","시간 초과":"Timed out","시계 맞춤":"Clock set","시계 실패":"Clock failed","시선 이동":"Gaze reach","시작 실패":"Start failed","아랫눈꺼풀":"Lower lid","안광 이동":"Shine travel","안광 크기":"Shine size","연결 끊김":"Disconnected","연결 실패":"Connection failed","유체 물리":"Fluid physics","저장 실패":"Save failed","최대 가속":"Max acceleration","켜고 끄기":"On and off","타원↔네모":"Oval↔square","터치 보정":"Touch calibration","픽셀 크기":"Pixel size","^ 아치":"^ arch","눈 간격":"Eye spacing","눈 높이":"Eye height","눈 세트":"Eye set","동그라미":"Round","로그인됨":"Logged in","미리보기":"Preview","불투명도":"Opacity","비밀번호":"Password","연결 중":"Connecting","윗눈꺼풀":"Upper lid","점 눈썹":"Dot brows","지느러미":"Fin","통통 튐":"Bounce","표면장력":"Surface tension","강낭콩":"Bean","거절됨":"Refused","고양이":"Cat","기울기":"Tilt","꾸미기":"Accessories","모서리":"Corners","목도리":"Scarf","물방울":"Water drop","반영됨":"Applied","반짝이":"Shiny","보내기":"Send","복원력":"Stiffness","볼터치":"Blush","사용자":"User","새싹이":"Sprout","색 1":"Color 1","색 2":"Color 2","슬라임":"Slime","안테나":"Antenna","연결됨":"Connected","오른손":"Right hand","움직임":"Motion","저장됨":"Saved","찹쌀떡":"Mochi","초기화":"Reset","출렁임":"Wobble","가로":"Landscape","가시":"Spikes","거절":"Deny","광택":"Gloss","기본":"Normal","기쁨":"Happy","길이":"Length","깃발":"Flag","꺼짐":"Off","날개":"Wings","너비":"Width","놀람":"Surprised","높이":"Height","눈썹":"Brows","달걀":"Egg","두께":"Thickness","레벨":"Level","로봇":"Robot","리본":"Bow","머리":"Head","받음":"In","보냄":"out","밤불":"Night flame","방울":"Bubble","배경":"Background","새싹":"Sprout","색상":"Colors","서리":"Frost","세로":"Portrait","슬픔":"Sad","승인":"Approve","아치":"Arch","아침":"Morning","안광":"Shine","애니":"Anime","언어":"Language","없음":"None","엠버":"Ember","오류":"Error","왕관":"Crown","왼손":"Left hand","이름":"Name","일자":"Line","잎새":"Leaf","자동":"Auto","저녁":"Evening","저장":"Save","적용":"Apply","점성":"Damping","졸림":"Sleepy","채도":"Saturation","천사":"Angel","크기":"Size","토픽":"Topic","파티":"Party","팔만":"Arm only","포트":"Port","풀밭":"Grass","풍선":"Balloon","하트":"Heart","화남":"Angry","낮":"Day","눈":"Eyes","몸":"Body","밤":"Night","배":"Belly","별":"Star","볼":"Cheeks","뿔":"Horns","색":"Color"};
const I18N_PARTS=Object.keys(I18N).filter(k=>k.length>1).sort((a,b)=>b.length-a.length);
let uiLang='ko';try{uiLang=localStorage.getItem('blob.lang')||'ko'}catch(e){}
function tr(s){if(uiLang!=='en'||!/[가-힣]/.test(s))return s;const t=s.trim();
  if(I18N[t])return s.replace(t,I18N[t]);
  for(const k of I18N_PARTS)if(s.includes(k))s=s.split(k).join(I18N[k]);return s}
// Each node keeps its Korean source; writing only what changes keeps the observer from feeding itself
function trNode(n){
  if(n.nodeType===3){if(n._done!==n.nodeValue)n._ko=n.nodeValue;const t=tr(n._ko);n._done=t;
    if(n.nodeValue!==t)n.nodeValue=t;return}
  if(n.nodeType!==1)return;
  if(n.placeholder){if(n.placeholder!==n._phDone)n._ph=n.placeholder;const t=tr(n._ph);n._phDone=t;
    if(n.placeholder!==t)n.placeholder=t}
  for(const c of n.childNodes)trNode(c)}
new MutationObserver(ms=>{for(const m of ms){if(m.type==='characterData')trNode(m.target);
  else{for(const n of m.addedNodes)trNode(n);if(m.type==='attributes')trNode(m.target)}}})
  .observe(document.body,{childList:true,subtree:true,characterData:true,attributes:true,attributeFilter:['placeholder']});
function setUiLang(l){uiLang=l;try{localStorage.setItem('blob.lang',l)}catch(e){}
  document.documentElement.lang=l;trNode(document.body)}
const EXPR={normal:'기본',happy:'기쁨',surprised:'놀람',sleepy:'졸림',angry:'화남',sad:'슬픔'};
const COLORS=[['background','배경'],['body','몸'],['eye','눈'],['shine','안광'],['cheek','볼']];
const SLOTS=[['head','머리'],['back','등 (몸 뒤)'],['left','왼손'],['right','오른손']];
const ITEM={none:'없음',antenna:'안테나',sprout:'새싹',bow:'리본',ears:'고양이 귀',horns:'뿔',crown:'왕관',
 wings:'날개',fin:'지느러미',spikes:'가시',arm:'팔만',heart:'하트',star:'별',flag:'깃발',balloon:'풍선'};
const SUGGEST={antenna:['#ffd25a','#5a6078'],sprout:['#78dc78','#46964f'],bow:['#ff6fa0','#ffd1e0'],ears:[null,'#ff9cc0'],
 horns:['#ffe8a0','#c8a050'],crown:['#ffd24a','#ff5a7a'],wings:['#f0f8ff','#b4d7ff'],fin:[null,'#a8d8ff'],spikes:['#7a86ff','#c0c8ff'],
 heart:['#ff5a78','#c8cde1'],star:['#ffd24a','#c8cde1'],flag:['#ff5a5a','#c8cde1'],balloon:['#ff7ab0','#c8cde1']};
const ACC=[['size','크기',.3,2.5,.05],['angle','위치(각도)',-90,90,1]];
const CHAR={slime:'슬라임',monster:'픽셀 몬스터',heroine:'애니',capri:'Capri'};
const CAP_LANG={ko:'한국어',en:'English'};
const CAP_TOGGLES=[['cutins','클로즈업 컷인'],['sound','소리 (스피커 있는 보드)'],['voice','영어 목소리 (brain의 /tts)'],['brain','길게 누르면 brain에게 말 걸기']];
const CAP_MOTION=[['textMs','글자 속도(ms)',5,150,1],['sleepAfter','졸기까지(초)',10,900,10],['chatter','먼저 말 거는 간격(분, 0=끔)',0,240,5],
 ['glitch','글리치 빈도',0,100,1],['volume','소리 크기',0,100,1]];
const PRESET={aqua:'물방울',sprout:'새싹이',kitty:'고양이',robo:'로봇',angel:'천사',imp:'꼬마 악마',royal:'왕관',party:'파티',mochi:'찹쌀떡',
 ember:'엠버',leaf:'잎새',bubble:'방울',frost:'서리',shiny:'반짝이',night:'밤불'};
const HERO_TIME={auto:'자동',morning:'아침',day:'낮',evening:'저녁',night:'밤'};
const HERO_MOTION=[['textMs','글자 속도(ms)',5,150,1],['sleepAfter','졸기까지(초)',10,900,10]];
const HERO_PANEL=[['saturation','채도',.5,2.5,.05],['gamma','감마 (클수록 밝은 톤이 진해짐)',.6,1.8,.02]];
const MON_MOTION=[['scale','픽셀 크기',2,4,1],['walkSpeed','걷는 속도',2,60,1],['textMs','글자 속도(ms)',5,150,1],
 ['sleepAfter','잠들기까지(초)',5,600,5],['level','레벨',1,100,1]];
const SHAPE={round:'동그라미',egg:'달걀',mochi:'찹쌀떡',drop:'물방울',squircle:'둥근 사각',cat:'고양이',bean:'강낭콩'};
const BROW={none:'없음',line:'일자',arch:'아치',thick:'굵은 일자',dot:'점 눈썹'};
const BROW_SET=[['thickness','두께',.01,.12,.005],['length','길이',.3,2,.05],['lift','눈과의 간격',-.1,.4,.01]];
const BODY=[['size','크기',.08,.4,.01],['aspect','가로/세로 비율',.6,1.6,.01],['opacity','불투명도',0,32,1],['highlight','광택',0,32,1],['cheekAlpha','볼터치',0,32,1]];
const EYE_LAYOUT=[['eyeSpacing','눈 간격',0,.9,.01],['eyeHeight','눈 높이',-.6,.6,.01],['gazeReach','시선 이동',0,.3,.01],
 ['shineTravel','안광 이동',0,.8,.01]];
const EYE=[['w','너비',.04,.8,.01],['h','높이',.04,.8,.01],['square','타원↔네모',0,1,.01],['corner','모서리',0,1,.01],
 ['tilt','기울기',-1,1,.01],['lidTop','윗눈꺼풀',0,1,.01],['lidAngle','눈꺼풀 각도',-1,1,.01],['lidBottom','아랫눈꺼풀',0,1,.01],
 ['arc','^ 아치',0,1,.01],['shine','안광 크기',0,2,.05],['browRaise','눈썹 높이',-.3,.4,.01],['browAngle','눈썹 각도',-1,1,.01]];
const MOTION=[['glideMs','이동 시간(ms)',120,1500,10],['thud','도착 충격(텅)',0,1,.01],['drop','꼬리 늘어남',0,3,.05],
 ['splash','물방울 떨어짐(실험, 0/1)',0,1,1],['bounce','통통 튐',0,8,.1]];
// Whole-physics presets: the default; a squishy toy that comes back slowly
// without bouncing; a watery slime that keeps wobbling
const PHYS_PRESETS=[['기본',{stiffness:160,damping:5,tension:900,volumeKeep:.5,inertia:.7,maxAccel:6000}],
 ['말랑이 (끈적)',{stiffness:35,damping:16,tension:1000,volumeKeep:.7,inertia:1.2,maxAccel:6000}],
 ['슬라임 (물 같은)',{stiffness:130,damping:3,tension:600,volumeKeep:.45,inertia:1,maxAccel:8000}]];
const PHYS=[['stiffness','복원력',20,600,5],['damping','점성',0,20,.1],['tension','표면장력',0,3000,10],
 ['volumeKeep','부피 유지',0,1,.01],['inertia','출렁임',0,2,.01],['maxAccel','최대 가속',1000,20000,100]];

const $=s=>document.querySelector(s);
// cfg = settings of the active character ch, M = its option lists from /api/meta
let cfg=null,meta=null,ch=null,M=null,tab='normal',timer=null,pending={},lastPreview=0;

function status(t){$('#status').textContent=t}
function deepMerge(a,b){for(const k in b){if(b[k]&&typeof b[k]==='object'){a[k]=a[k]||{};deepMerge(a[k],b[k])}else a[k]=b[k]}}
async function post(url,body){
  status('…');
  try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
    const j=await r.json();status(r.ok?'반영됨':'오류');return j}
  catch(e){status('연결 끊김')}
}
// /api/config answers {"character":ch,[ch]:{...}}; patches go under the character's name
function take(j){if(!j||!j.character)return false;ch=j.character;cfg=j[ch];M=meta.characters.find(c=>c.name===ch);return true}
function send(patch){deepMerge(pending,{[ch]:patch});clearTimeout(timer);timer=setTimeout(()=>{const p=pending;pending={};post('/api/config',p)},60)}
function preview(expr,force){const now=Date.now();if(force||now-lastPreview>1500){lastPreview=now;post('/api/preview',{expr})}}

function slider(el,label,value,min,max,step,on){
  const r=document.createElement('div');r.className='row';
  r.innerHTML=`<label>${label}</label><input type=range min=${min} max=${max} step=${step}><output></output>`;
  const i=r.querySelector('input'),o=r.querySelector('output');i.value=value;o.textContent=value;
  i.oninput=()=>{o.textContent=i.value;on(parseFloat(i.value))};el.appendChild(r)}
function color(el,label,value,on){
  const r=document.createElement('div');r.className='row';
  r.innerHTML=`<label>${label}</label><input type=color><output></output>`;
  const i=r.querySelector('input');i.value=value;i.oninput=()=>on(i.value);el.appendChild(r)}
function chips(el,items,active,on){
  el.innerHTML='';for(const[k,label]of items){const b=document.createElement('button');b.textContent=label;
    if(k===active)b.className='on';b.onclick=()=>on(k);el.appendChild(b)}}
function group(el,schema,obj,patch){
  el.innerHTML='';for(const[k,label,min,max,step]of schema)slider(el,label,obj[k],min,max,step,v=>{obj[k]=v;send(patch(k,v))})}

async function pick(url,body){if(take(await post(url,body)))render()}
function edit(patch){return pick('/api/config',{[ch]:patch})}
function render(){
  chips($('#chars'),meta.characters.map(c=>[c.name,CHAR[c.name]||c.name]),ch,c=>pick('/api/config',{character:c}));
  chips($('#presets'),M.presets.map(p=>[p,PRESET[p]||p]),null,p=>pick('/api/preset',{name:p}));
  chips($('#exprs'),Object.entries(EXPR),null,e=>preview(e,true));
  for(const c of meta.characters){const ed=$('#ed-'+c.name);if(ed)ed.style.display=c.name===ch?'':'none'}
  if(ch==='slime')renderSlime();
  else if(ch==='monster')renderMonster();
  else if(ch==='heroine')renderHeroine();
  else if(ch==='capri')renderCapri();
}
function renderCapri(){
  const n=$('#capCall');n.innerHTML='';
  const r=document.createElement('div');r.className='row';r.innerHTML='<label>이름</label><input type=text maxlength=8><output></output>';
  const i=r.querySelector('input');i.value=cfg.call;i.onchange=()=>edit({call:i.value});n.appendChild(r);
  chips($('#capLang'),M.langs.map(l=>[l,CAP_LANG[l]||l]),cfg.lang,l=>edit({lang:l}));
  const c=$('#capColors');c.innerHTML='';
  color(c,'색',cfg.accent,v=>{cfg.accent=v;send({accent:v})});
  const t=$('#capToggles');t.innerHTML='';
  for(const[k,label]of CAP_TOGGLES){const b=document.createElement('button');b.textContent=label;if(cfg[k])b.className='on';
    b.onclick=()=>edit({[k]:!cfg[k]});t.appendChild(b)}
  group($('#capMotion'),CAP_MOTION,cfg,(k,v)=>({[k]:v}));
}
function renderHeroine(){
  const n=$('#heroName');n.innerHTML='';
  const r=document.createElement('div');r.className='row';r.innerHTML='<label>이름</label><input type=text maxlength=8><output></output>';
  const i=r.querySelector('input');i.value=cfg.name;i.onchange=()=>edit({name:i.value});n.appendChild(r);
  const c=$('#heroColors');c.innerHTML='';
  color(c,'색',cfg.accent,v=>{cfg.accent=v;send({accent:v})});
  chips($('#heroTime'),M.times.map(t=>[t,HERO_TIME[t]||t]),cfg.time,t=>edit({time:t}));
  group($('#heroMotion'),HERO_MOTION,cfg,(k,v)=>({[k]:v}));
  group($('#heroPanel'),HERO_PANEL,cfg,(k,v)=>({[k]:v}));
}
function renderMonster(){
  // Starter choice = that starter's preset; accent colors are named after its feature (art/monster/sprites.json)
  const sp=M.species,starter=sp.find(s=>s.id===cfg.species)||sp[0];
  chips($('#monStarter'),sp.map(s=>[s.preset,s.label+' · '+s.name]),starter.preset,p=>pick('/api/preset',{name:p}));
  const n=$('#monName');n.innerHTML='';
  const r=document.createElement('div');r.className='row';r.innerHTML='<label>이름</label><input type=text maxlength=8><output></output>';
  const i=r.querySelector('input');i.value=cfg.name;i.onchange=()=>edit({name:i.value});n.appendChild(r);
  const a=starter.accentNames,c=$('#monColors');c.innerHTML='';
  for(const[k,label]of[['body','몸'],['belly','배'],['accent',a[0]],['accent2',a[1]],['scarf','목도리'],['grass','풀밭']])
    color(c,label,cfg[k],v=>{cfg[k]=v;send({[k]:v})});
  group($('#monMotion'),MON_MOTION,cfg,(k,v)=>({[k]:v}));
}
function renderSlime(){
  chips($('#shapes'),M.bodyShapes.map(s=>[s,SHAPE[s]||s]),cfg.bodyShape,s=>edit({bodyShape:s}));
  chips($('#browStyles'),M.browStyles.map(s=>[s,BROW[s]||s]),cfg.brows.style,s=>edit({brows:{style:s}}));
  const bw=$('#brows');bw.innerHTML='';
  if(cfg.brows.style!=='none'){
    color(bw,'색',cfg.brows.color,v=>{cfg.brows.color=v;send({brows:{color:v}})});
    for(const[k,l,min,max,step]of BROW_SET)slider(bw,l,cfg.brows[k],min,max,step,v=>{cfg.brows[k]=v;send({brows:{[k]:v}})});
  }
  const c=$('#colors');c.innerHTML='';
  for(const[k,label]of COLORS)color(c,label,cfg[k],v=>{cfg[k]=v;send({[k]:v})});
  group($('#body'),BODY,cfg,(k,v)=>({[k]:v}));
  chips($('#sets'),M.eyeSets.map(s=>[s,s]),cfg.eyeSet,s=>edit({eyeSet:s}));
  group($('#eyeLayout'),EYE_LAYOUT,cfg,(k,v)=>({[k]:v}));
  chips($('#tabs'),Object.entries(EXPR),tab,e=>{tab=e;render();preview(e,true)});
  group($('#eyeShape'),EYE,cfg.eyes[tab],(k,v)=>{preview(tab);return{eyes:{[tab]:{[k]:v}}}});
  const acc=$('#acc');acc.innerHTML='';
  for(const[slot,label]of SLOTS){
    const a=cfg.accessories[slot],box=document.createElement('div');box.innerHTML=`<h2>${label}</h2>`;
    const row=document.createElement('div');row.className='chips';box.appendChild(row);
    chips(row,M.items[slot].map(t=>[t,ITEM[t]||t]),a.type,t=>{
      const p={type:t},s=SUGGEST[t];if(s){p.color=s[0]||cfg.body;p.color2=s[1]}
      edit({accessories:{[slot]:p}})});
    if(a.type!=='none'){
      color(box,'색 1',a.color,v=>{a.color=v;send({accessories:{[slot]:{color:v}}})});
      color(box,'색 2',a.color2,v=>{a.color2=v;send({accessories:{[slot]:{color2:v}}})});
      const g=document.createElement('div');box.appendChild(g);
      for(const[k,l,min,max,step]of ACC)slider(g,l,a[k],min,max,step,v=>{a[k]=v;send({accessories:{[slot]:{[k]:v}}})});
    }
    acc.appendChild(box);
  }
  group($('#motion'),MOTION,cfg.motion,(k,v)=>({motion:{[k]:v}}));
  const phys=PHYS_PRESETS.find(([,p])=>Object.keys(p).every(k=>cfg.physics[k]===p[k]));
  chips($('#physPresets'),PHYS_PRESETS.map(([n])=>[n,n]),phys&&phys[0],n=>edit({physics:PHYS_PRESETS.find(p=>p[0]===n)[1]}));
  group($('#physics'),PHYS,cfg.physics,(k,v)=>({physics:{[k]:v}}));
}

// The device has no clock of its own: lend it this phone's or PC's time
async function syncClock(){const j=await post('/api/time',{epoch:Math.floor(Date.now()/1000),offset:-new Date().getTimezoneOffset()});
  status(j&&j.ok?'시계 맞춤 '+new Date().toTimeString().slice(0,5):'시계 실패')}
$('#clock').onclick=syncClock;
$('#save').onclick=async()=>{const j=await post('/api/save');status(j&&j.ok?'저장됨':'저장 실패')};
$('#reset').onclick=async()=>{if(take(await post('/api/reset'))){render();status('초기값 (저장 전)')}};
$('#tNotify').onclick=()=>post('/api/card',{kind:'notification',title:'새 메시지',body:'빌드가 끝났어요. 테스트가 모두 통과했습니다.'});
$('#tTouch').onclick=()=>post('/api/touchcal',{});
$('#capSend').onclick=()=>{const i=$('#capSay'),t=i.value.trim();if(t){post('/api/ask',{type:'chat',text:t});i.value=''}};
$('#capSay').onkeydown=e=>{if(e.key==='Enter')$('#capSend').onclick()};
$('#tPrompt').onclick=()=>post('/api/card',{kind:'prompt',title:'배포 요청',body:'v1.2를 운영 서버에 배포할까요?',options:['승인','거절']});

// Screen rotation (clockwise degrees) and mirror: saved on the device, which restarts into them
const ROTATION=[[0,'가로'],[90,'세로'],[180,'가로 (뒤집기)'],[270,'세로 (뒤집기)']];
let screen=null;
function renderScreen(){
  chips($('#rotation'),ROTATION,screen.rotation,deg=>setScreen({rotation:deg}));
  $('#mirror').className=screen.mirror?'on':''}
async function setScreen(change){
  const j=await post('/api/display',change);
  if(!j||j.rotation===undefined){status('화면 방향 바꾸기 실패');return}
  screen=j;renderScreen();
  if(j.restart){status('다시 시작하는 중…');setTimeout(()=>location.reload(),6000)}
}
$('#mirror').onclick=()=>screen&&setScreen({mirror:!screen.mirror});
(async()=>{try{const r=await fetch('/api/display');if(!r.ok)return;screen=await r.json();
  $('#disp').style.display='';renderScreen()}catch(e){}})();

// Home Wi-Fi + MQTT. Only the device has /api/net: elsewhere the section stays hidden
const NET_LINK={off:'꺼짐',connecting:'연결 중',connected:'연결됨'};
const NET_ERR={'no-ap':'Wi-Fi를 못 찾음',auth:'비밀번호나 계정이 틀림',other:'연결 실패',dns:'주소를 못 찾음',
 tcp:'브로커가 응답 안 함',timeout:'시간 초과',tls:'TLS 인증서 오류',refused:'브로커가 거절',network:'네트워크 오류',init:'시작 실패'};
// Passwords go out only when typed: left blank, the saved one stays
const NET_FIELDS=[['wifi','ssid','Wi-Fi 이름','text'],['wifi','pass','Wi-Fi 비밀번호','password'],
 ['mqtt','host','브로커 주소','text'],['mqtt','port','포트','number'],['mqtt','tls','TLS','checkbox'],
 ['mqtt','user','사용자','text'],['mqtt','pass','비밀번호','password'],['mqtt','topic','토픽','text'],
 ['aibuddy','device','AI Buddy 기기 번호 (예: HB-0006, 비우면 시험용 토픽)','text'],['aibuddy','secret','AI Buddy 비밀키','password']];
const AIB_STATE={off:'꺼짐',connecting:'연결 중','need-otp':'OTP 필요','logging-in':'로그인 중','logged-in':'로그인됨',refused:'거절됨'};
let net=null;
function netLine(g,label){const s=net[g];let t=label+' '+(NET_LINK[s.status]||s.status);
  if(s.error)t+=` (${NET_ERR[s.error]||s.error})`;return t}
function renderNetState(){
  let t=netLine('wifi','집 Wi-Fi');if(net.wifi.ip)t+=` · http://${net.wifi.ip}/`;
  const a=net.aibuddy,aib=a.device;t+=' · '+netLine('mqtt','MQTT');
  if(aib)t+=` · AI Buddy ${aib} ${AIB_STATE[a.state]||a.state}${a.user?' ('+a.user+')':''}`;
  $('#netState').textContent=t;
  $('#otpRow').style.display=aib&&(a.state==='need-otp'||a.state==='refused')?'':'none';
  const p=net.mqtt.topic;$('#netTopics').textContent=aib?`AI Buddy: 받음 d/${aib}/dn · 보냄 d/${aib}/up, d/${aib}/st`
    :`받음: ${p}/card, ${p}/time · 보냄: ${p}/outcome, ${p}/status`}
function renderNetForm(){
  const f=$('#netForm');f.innerHTML='';
  for(const[g,k,label,type]of NET_FIELDS){
    const r=document.createElement('div');r.className='row';
    r.innerHTML=`<label>${label}</label><input type=${type}><output></output>`;
    const i=r.querySelector('input');i.dataset.g=g;i.dataset.k=k;
    if(type==='checkbox')i.checked=net[g][k];
    else if(type==='password'){i.placeholder=(net[g].passSet||net[g].secretSet)?'저장됨 (바꿀 때만 입력)':'없음';i.oninput=()=>i.dataset.typed=1}
    else if(k==='port'){i.value=net[g].port||'';i.placeholder='1883 (TLS 8883)'}
    else i.value=net[g][k];
    f.appendChild(r)}}
async function loadNet(form){try{const r=await fetch('/api/net');if(!r.ok)return;net=await r.json();
  $('#net').style.display='';renderNetState();if(form)renderNetForm()}catch(e){}}
$('#netApply').onclick=async()=>{
  const body={wifi:{},mqtt:{},aibuddy:{}};
  for(const i of document.querySelectorAll('#netForm input')){
    const{g,k}=i.dataset;
    if(i.type==='checkbox')body[g][k]=i.checked;
    else if(i.type==='password'){if(i.dataset.typed)body[g][k]=i.value}
    else if(k==='port')body[g][k]=parseInt(i.value)||0;
    else body[g][k]=k==='ssid'?i.value:i.value.trim()}
  const j=await post('/api/net',body);
  if(j&&j.wifi){net=j;renderNetState();renderNetForm();status('네트워크 저장됨')}else status('네트워크 설정 오류')};
loadNet(true);setInterval(()=>{if(net)loadNet(false)},3000);
$('#otpSend').onclick=async()=>{const j=await post('/api/otp',{code:$('#otpCode').value.trim()});
  status(j&&j.ok?'OTP 보냄':'OTP를 받을 로그인이 없음 (6자리 숫자)');$('#otpCode').value=''};

$('#uiLang').value=uiLang;$('#uiLang').onchange=e=>setUiLang(e.target.value);setUiLang(uiLang);
(async()=>{try{meta=await(await fetch('/api/meta')).json();take(await(await fetch('/api/config')).json());render();syncClock()}
 catch(e){status('불러오기 실패')}})();
</script></body></html>
)HTML";
