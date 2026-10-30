"""Battery page for a blob device: http://localhost:8772

Joins the MQTT broker (tools/mqtt/blob_mqtt.py broker) as a client, keeps what the device
sends on <topic>/battery and shows it live; the button asks for a reading at once
(<topic>/battery/get). Readings are kept in memory only.

  python tools/mqtt/battery_web.py [--host 127.0.0.1] [--topic blob/4c08] [--http 8772] [--load history.json]

The percentage is read off a general LiPo curve (CURVE in the page), not measured on the
cell: an estimate until the board's own discharge and charge curves replace it.
--load starts with the readings of an earlier run (the JSON of /api/battery).
"""
import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import blob_mqtt

PAGE = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>배터리</title>
<style>
:root{--bg:#fff;--fg:#1a1a1a;--dim:#6b6b6b;--line:#d8d8d8;--acc:#2a6bd6}
@media(prefers-color-scheme:dark){:root{--bg:#161616;--fg:#eee;--dim:#9a9a9a;--line:#3a3a3a;--acc:#6aa3ff}}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif;max-width:720px}
h1{font-size:15px;font-weight:600;color:var(--dim);margin:0 0 8px}
#now{font-size:44px;font-weight:600}#now small{font-size:18px;color:var(--dim);font-weight:400}
#meta{color:var(--dim);margin:4px 0 12px}
button{font:inherit;padding:8px 14px;border:1px solid var(--line);border-radius:8px;background:none;color:var(--fg)}
canvas{width:100%;height:200px;margin:16px 0 8px;border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%}td,th{padding:4px 8px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--dim);font-weight:400}
</style>
<h1 id="topic"></h1>
<div id="now">-</div><div id="meta"></div>
<button id="ask">지금 읽기</button>
<canvas id="c" width="720" height="200"></canvas>
<div id="sm" style="color:var(--dim);margin:8px 0 0"></div>
<table><thead><tr><th>시각<th>전압<th>중앙값(5)<th>이동평균(5)<th>종류</tr></thead><tbody id="rows"></tbody></table>
<script>
const $=id=>document.getElementById(id);
// The firmware's table (src/app/core/battery.h): general LiPo values with the top moved to the
// 4.07 V this board measured as full at rest; under load the voltage reads lower, while charging
// it reads higher, so this is an estimate.
const CURVE=[[3300,0],[3600,5],[3700,15],[3750,25],[3800,40],[3850,55],[3950,70],[4050,92],[4070,100]];
function pct(mv){
  if(mv<=CURVE[0][0])return 0;
  for(let i=1;i<CURVE.length;i++){
    const [v1,p1]=CURVE[i],[v0,p0]=CURVE[i-1];
    if(mv<=v1)return Math.round(p0+(p1-p0)*(mv-v0)/(v1-v0));
  }
  return 100;
}
// Median and moving average of the last W readings (counted, not timed: readings come every
// 30 s to 5 minutes), for each reading.
const W=5;
function median(a){const s=[...a].sort((x,y)=>x-y),m=s.length>>1;return s.length%2?s[m]:(s[m-1]+s[m])/2}
function smooth(d){
  return d.map((_,i)=>{
    const w=d.slice(Math.max(0,i-W+1),i+1).map(x=>x.mv);
    return {med:Math.round(median(w)),avg:Math.round(w.reduce((a,b)=>a+b,0)/w.length)};
  });
}
function draw(d){
  const c=$('c'),g=c.getContext('2d'),w=c.width,h=c.height,st=getComputedStyle(document.body);
  g.clearRect(0,0,w,h);if(d.length<2)return;
  const v=d.map(x=>x.mv),lo=Math.min(...v)-20,hi=Math.max(...v)+20,t0=d[0].t,t1=d[d.length-1].t||t0+1;
  g.strokeStyle=st.getPropertyValue('--acc');g.lineWidth=2;g.beginPath();
  d.forEach((x,i)=>{const X=(x.t-t0)/(t1-t0||1)*(w-20)+10,Y=h-10-(x.mv-lo)/(hi-lo)*(h-20);i?g.lineTo(X,Y):g.moveTo(X,Y)});
  g.stroke();g.fillStyle=st.getPropertyValue('--dim');g.font='12px sans-serif';
  g.fillText(hi.toFixed(0)+' mV',12,14);g.fillText(lo.toFixed(0)+' mV',12,h-14);
}
async function tick(){
  try{
    const r=await (await fetch('/api/battery')).json();
    $('topic').textContent=r.topic+'/battery  ·  '+(r.connected?'브로커 연결됨':'브로커 연결 안 됨');
    const d=r.readings,last=d[d.length-1];
    $('now').innerHTML=last?(last.mv/1000).toFixed(3)+' <small>V · 약 '+pct(last.mv)+'% (추정)</small>':'-';
    $('meta').textContent=last?new Date(last.t*1000).toLocaleTimeString()+' 수신 ('+Math.round(Date.now()/1000-last.t)+'초 전)':'아직 받은 값이 없습니다';
    const sm=smooth(d),off=Math.max(0,d.length-40);
    $('sm').textContent=last?'중앙값 '+(sm[sm.length-1].med/1000).toFixed(3)+' V ('+pct(sm[sm.length-1].med)+'%)  ·  이동평균 '+(sm[sm.length-1].avg/1000).toFixed(3)+' V ('+pct(sm[sm.length-1].avg)+'%)  ·  최근 '+W+'개 기준'+(last.pct!=null?'  ·  기기가 보고한 값 '+last.pct+'%':''):'';
    $('rows').innerHTML=d.slice(-40).map((x,k)=>({x,s:sm[off+k]})).reverse().map(({x,s})=>'<tr><td>'+new Date(x.t*1000).toLocaleTimeString()+'<td>'+x.mv+' mV ('+pct(x.mv)+'%)<td>'+s.med+' mV ('+pct(s.med)+'%)<td>'+s.avg+' mV ('+pct(s.avg)+'%)<td>'+(x.retained?'저장된 값':'새 값')+'</tr>').join('');
    draw(d);
  }catch(e){$('meta').textContent='서버에 연결할 수 없습니다'}
}
$('ask').onclick=async()=>{$('ask').disabled=true;await fetch('/api/ask',{method:'POST'});setTimeout(()=>{$('ask').disabled=false;tick()},1200)};
tick();setInterval(tick,2000);
</script>"""

state = {"readings": [], "connected": False}
lock = threading.Lock()
client = None


def mqtt_loop(host, port, topic):
    global client
    while True:
        try:
            c = blob_mqtt.Client(host, port)
            c.subscribe(topic + "/battery")
            client = c
            with lock:
                state["connected"] = True
            for _, payload, retained in c.messages(None):
                try:
                    data = json.loads(payload)
                    mv = int(data["mv"])
                except (ValueError, KeyError, TypeError):
                    continue
                with lock:
                    # "pct" is the device's own percentage (newer firmware), the page also works it out
                    state["readings"].append({"t": time.time(), "mv": mv, "pct": data.get("pct"), "retained": retained})
                    del state["readings"][:-2000]
        except (OSError, SystemExit):
            pass
        client = None
        with lock:
            state["connected"] = False
        time.sleep(3)


def make_handler(topic):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, code, body, kind):
            data = body.encode()
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path == "/api/battery":
                with lock:
                    body = json.dumps({"topic": topic, **state})
                self.reply(200, body, "application/json")
            else:
                self.reply(200, PAGE, "text/html; charset=utf-8")

        def do_POST(self):
            c = client
            if self.path == "/api/ask" and c:
                try:
                    c.publish(topic + "/battery/get", b"", qos=0)
                    return self.reply(200, "{}", "application/json")
                except OSError:
                    pass
            self.reply(503, "{}", "application/json")

        def log_message(self, *args):
            pass

    return Handler


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1", help="broker address")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--topic", default="blob/4c08", help="device topic")
    p.add_argument("--http", type=int, default=8772)
    p.add_argument("--load", help="JSON of /api/battery from an earlier run, kept in the history")
    args = p.parse_args()
    if args.load:
        with open(args.load, encoding="utf-8-sig") as f:
            state["readings"] = json.load(f)["readings"]
    threading.Thread(target=mqtt_loop, args=(args.host, args.port, args.topic), daemon=True).start()
    print(f"battery page for {args.topic}: http://localhost:{args.http}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", args.http), make_handler(args.topic)).serve_forever()


if __name__ == "__main__":
    main()
