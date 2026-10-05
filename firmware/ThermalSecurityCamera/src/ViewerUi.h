#pragma once
#include <Arduino.h>
namespace thermal {
static const char ViewerUi[] PROGMEM=R"THERMAL(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Thermal monitor</title><style>
:root{color-scheme:dark;font:15px system-ui,-apple-system,"Segoe UI",sans-serif;background:#0b1013;color:#edf3f1;--muted:#a1b3ad;--line:#2b3934;--green:#7ef5a7;--amber:#ffd183;--red:#ff9b99}
*{box-sizing:border-box}body{margin:0}button,input,select{font:inherit}button,a.button{display:inline-flex;align-items:center;justify-content:center;min-height:42px;padding:10px 15px;border:1px solid #44564e;border-radius:9px;background:#18231e;color:#edf3f1;font-weight:600;text-decoration:none;cursor:pointer}button:hover,a.button:hover{background:#24352c;border-color:#87a795}button:disabled{opacity:.55;cursor:wait}button.primary{background:var(--green);border-color:var(--green);color:#092d17}a{color:var(--green)}:focus-visible{outline:2px solid var(--green);outline-offset:4px}h1,h2,p{margin:0}h1{font-size:23px;letter-spacing:-.6px;font-weight:650}h2{font-size:18px;font-weight:600}p,small{line-height:1.55}small,.muted{color:var(--muted)}.shell{max-width:1440px;margin:auto;padding:26px 32px}.topbar{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:24px}.brand{display:flex;align-items:center;gap:13px}.brand-mark{display:grid;grid-template-columns:repeat(2,7px);gap:4px;padding:13px;border:1px solid #365044;border-radius:11px;background:#15261c}.brand-mark i{width:7px;height:7px;background:var(--green);border-radius:1px}.eyebrow{display:block;color:var(--muted);font-size:11px;font-weight:650;letter-spacing:1.5px;text-transform:uppercase;margin-bottom:5px}.actions{display:flex;gap:9px;flex-wrap:wrap;align-items:center}.connection{font-size:12px;color:var(--muted);display:flex;gap:7px;align-items:center}.connection::before{content:"";width:6px;height:6px;border-radius:50%;background:currentColor}.connection[data-state=online]{color:var(--green)}.connection[data-state=offline]{color:var(--red)}.panel{min-width:0;border:1px solid var(--line);border-radius:14px;background:#121b17;overflow:hidden}.panel-heading{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:17px 20px;border-bottom:1px solid var(--line)}.panel-heading h2{font-size:14px}.section-note{font-size:12px;color:var(--muted)}#view{position:relative;aspect-ratio:4/3;background:#030605}#feed,canvas{position:absolute;width:100%;height:100%;inset:0}#feed{object-fit:contain;image-rendering:pixelated}#feed:not([src]){visibility:hidden}#stale{position:absolute;inset:0;display:grid;place-content:center;text-align:center;padding:20px;background:#090f0ee8;font-weight:500;color:#d4e1db;pointer-events:none}.stats{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;padding:16px 20px;font-size:13px;color:var(--muted);font-variant-numeric:tabular-nums}.stats strong{display:block;color:#edf3f1;font-size:15px;font-weight:550;margin-top:4px}.det-panel{min-width:0;padding:22px;border:1px solid #35423c;border-radius:14px;background:#16201b;transition:background-color .18s,border-color .18s,box-shadow .18s}.det-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:22px}.channel-number{font-size:10px;letter-spacing:1.6px;color:var(--muted);font-weight:650}.signal{width:10px;height:10px;border-radius:50%;background:#708379;border:1px solid #92a69b}.det-panel h2{font-size:15px;color:#c9d8d0;margin-bottom:7px}.det-state{font-size:29px;font-weight:650;letter-spacing:-.6px;line-height:1.2}.det-details{display:flex;justify-content:space-between;gap:10px;margin-top:22px;padding-top:15px;border-top:1px solid #ffffff14;color:var(--muted);font-size:12px;font-variant-numeric:tabular-nums}.det-details strong{display:block;font-size:15px;color:#dce9e1;margin-top:4px;font-weight:500}.det-panel[data-state=occupied]{background:#123822;border-color:#75ed9c;box-shadow:0 0 26px #5fff981a,inset 0 1px 0 #a1ffbd22}.det-panel[data-state=occupied] .det-state{color:#95ffb7}.det-panel[data-state=occupied] .signal{background:#8affad;border-color:#b9ffd0;box-shadow:0 0 13px #64ff96a0}.det-panel[data-state=learning]{border-color:#8e723c;background:#29251b}.det-panel[data-state=learning] .det-state{color:var(--amber)}.det-panel[data-state=learning] .signal{background:var(--amber);border-color:var(--amber)}.det-panel[data-state=unavailable]{border-color:#855050;background:#2b1d1e}.det-panel[data-state=unavailable] .det-state{color:var(--red)}.det-panel[data-state=unavailable] .signal{background:var(--red);border-color:var(--red)}.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}.page-note{color:var(--muted);font-size:12px;margin-top:18px;line-height:1.6}#test{padding:12px 16px;border:1px solid #80577c;border-radius:9px;color:#ffc1f5;background:#281d29;margin-bottom:18px}.feedback{color:#cbd9d2;font-size:13px;white-space:pre-line}#screenMessage:empty{display:none}
@media(max-width:899px){.shell{padding:20px}.topbar{align-items:flex-start}.brand-mark{display:none}}
@media(max-width:480px){.shell{padding:14px}.topbar{flex-direction:column;gap:16px;margin-bottom:18px}h1{font-size:22px}.actions{width:100%}.actions .connection{margin-right:auto}.panel-heading,.stats{padding:14px}.det-panel{padding:18px}.det-state{font-size:clamp(20px,5.5vw,26px)}}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{transition:none!important;scroll-behavior:auto!important}}

.dashboard{display:grid;grid-template-columns:minmax(0,1fr) 280px;gap:22px;align-items:start}.detection-column{display:grid;gap:16px}.viewer-camera{width:100%;max-width:calc((100dvh - 300px)*4/3);justify-self:center}.viewer-camera #view{min-height:0}.sidebar-note{font-size:12px;color:var(--muted);line-height:1.6;padding:2px 5px}
@media(max-width:899px){.dashboard{grid-template-columns:1fr}.viewer-camera{max-width:100%}.detection-column{grid-template-columns:repeat(2,minmax(0,1fr))}.sidebar-note{grid-column:1/-1}.det-top{margin-bottom:15px}}
@media(max-width:359px){.detection-column{grid-template-columns:1fr}}
@media(min-width:900px) and (max-height:600px){.viewer-camera{max-width:100%}}
</style></head><body><div class="shell"><header class="topbar"><div class="brand"><span class="brand-mark" aria-hidden="true"><i></i><i></i><i></i><i></i></span><div><span class="eyebrow">LIVE VIEW</span><h1>Thermal monitor</h1></div></div><nav class="actions" aria-label="Camera navigation"><span id="connection" class="connection">Connecting</span><button id="fullscreen" type="button">Full screen</button><a class="button" href="/settings">Settings</a></nav></header>
<p id="test" hidden>TEST PATTERN · Synthetic temperatures · MQTT disabled</p><p class="feedback" id="screenMessage" role="status"></p>
<main class="dashboard"><section class="panel viewer-camera" aria-label="Thermal video"><div class="panel-heading"><h2>Live thermal</h2><span class="section-note">Monitored region outlined</span></div><div id="view"><img id="feed" alt="Live thermal camera"><div id="stale">Connecting to camera…</div></div><div class="stats"><span>Temperature range<strong id="temperature">&mdash;</strong></span><span>Video<strong id="fps">&mdash;</strong></span><span>Detection<strong id="state">Connecting</strong></span></div></section><aside class="detection-column" aria-label="Object detection"><section class="det-panel" id="smallPanel" data-state="connecting" aria-labelledby="smallTitle"><div class="det-top"><span class="channel-number">01 / DETECTION</span><span class="signal" aria-hidden="true"></span></div><h2 id="smallTitle">Small objects</h2><p class="det-state" role="status" aria-live="polite" aria-atomic="true"><span class="sr-only">Small objects: </span><span id="smallState">Connecting</span></p><div class="det-details"><span>Region<strong id="smallPixels">&mdash;</strong></span><span>Peak<strong id="smallPeak">&mdash;</strong></span></div></section><section class="det-panel" id="largePanel" data-state="connecting" aria-labelledby="largeTitle"><div class="det-top"><span class="channel-number">02 / DETECTION</span><span class="signal" aria-hidden="true"></span></div><h2 id="largeTitle">Large objects</h2><p class="det-state" role="status" aria-live="polite" aria-atomic="true"><span class="sr-only">Large objects: </span><span id="largeState">Connecting</span></p><div class="det-details"><span>Region<strong id="largePixels">&mdash;</strong></span><span>Peak<strong id="largePeak">&mdash;</strong></span></div></section><p class="sidebar-note">Green means a warm object is detected in the monitored region. Size is measured in thermal pixels.</p></aside></main><footer class="page-note" id="health">Connecting to device…</footer></div>
<script>
const $=id=>document.getElementById(id),clock=()=>performance.now();
let started=false,nextStream=0,lastFrame=-1,frameAt=0,lastResponse=clock(),lastStatus=null,polling=false;
function text(id,value){const node=$(id);if(node&&node.textContent!==value)node.textContent=value}
function sensorFresh(s){return s&&s.sensor_ready===true&&Number.isFinite(s.frame_age_ms)&&s.frame_age_ms>=0&&s.frame_age_ms<1000&&clock()-frameAt<1500}
function cards(s,fresh){
  for(const name of ['small','large']){
    const channel=s&&s[name];let state=fresh&&channel?channel.state:'unavailable';
    if(!['clear','occupied','learning','unavailable'].includes(state))state='unavailable';
    const warming=state!=='unavailable'&&s.sensor_warmup_ms>0;if(warming)state='learning';
    $(name+'Panel').dataset.state=state;
    text(name+'State',warming?'Warming up':({occupied:'Detected',clear:'Clear',learning:'Learning',unavailable:'Unavailable'})[state]);
    const measured=state==='occupied'&&Number.isFinite(channel.pixels)&&channel.pixels>0;
    text(name+'Pixels',measured?channel.pixels+' pixels':'—');
    text(name+'Peak',measured&&Number.isFinite(channel.peak_c)?channel.peak_c.toFixed(1)+' °C':'—');
  }
}
function hideVideo(message){$('feed').removeAttribute('src');started=false;$('stale').style.display='grid';text('stale',message)}
function unavailable(message){
  cards(null,false);hideVideo(message);text('temperature','—');text('fps','—');text('state','Unavailable');
  text('connection','Connection lost');$('connection').dataset.state='offline';text('health',message);
}
async function request(path,options={},timeoutMs=8000){
  const controller=new AbortController();let timer;
  try{return await Promise.race([
    (async()=>{const response=await fetch(path,{cache:'no-store',...options,signal:controller.signal});const value=await response.json();if(!response.ok)throw Error(value.error||response.statusText);return value})(),
    new Promise((_,reject)=>{timer=setTimeout(()=>{controller.abort();reject(Error('Request timed out'))},timeoutMs)})
  ])}finally{clearTimeout(timer)}
}
function stream(){if(clock()<nextStream)return;nextStream=clock()+5000;$('feed').src='/stream.mjpg?t='+Date.now();started=true}
$('feed').onerror=()=>{started=false;$('stale').style.display='grid';text('stale','Reconnecting video…')};
function showStatus(s){
  if(!s||!Number.isFinite(s.frames))throw Error('Invalid status response');
  if(lastFrame!==s.frames){lastFrame=s.frames;frameAt=clock()}
  lastResponse=clock();lastStatus=s;
  const fresh=sensorFresh(s);cards(s,fresh);
  text('connection','Connected');$('connection').dataset.state='online';
  const settled=fresh&&!(s.sensor_warmup_ms>0);
  text('temperature',settled&&Number.isFinite(s.min_c)&&Number.isFinite(s.max_c)?s.min_c.toFixed(1)+' – '+s.max_c.toFixed(1)+' °C':fresh&&s.sensor_warmup_ms>0?'Warming up':'—');
  text('fps',fresh&&s.video_enabled&&Number.isFinite(s.video_fps)?s.video_fps.toFixed(1)+' FPS':s.video_enabled===false?'Paused':'—');
  text('state',!fresh?'Unavailable':s.sensor_warmup_ms>0?'Warming · '+Math.ceil(s.sensor_warmup_ms/1000)+'s':({occupied:'Detected',clear:'Clear',learning:'Learning',unavailable:'Unavailable'})[s.state]||'Unavailable');
  if($('test'))$('test').hidden=!s.test_pattern;
  text('health','Wi-Fi: '+(s.wifi_connected?'connected':'setup')+' · MQTT: '+(s.mqtt_connected?'connected':s.mqtt_configured?'disconnected':'disabled')+' · USB: '+s.usb);
  text('diagnostics',JSON.stringify(s,null,2));
  if(fresh&&s.video_enabled){$('stale').style.display='none';if(!started)stream()}
  else hideVideo(!fresh?(s.sensor_error&&s.sensor_error!=='none'?s.sensor_error:'Waiting for fresh sensor data'):'Video paused');
}
async function poll(){
  if(polling)return;polling=true;
  try{showStatus(await request('/api/status',{},2500))}
  catch(e){lastStatus=null;unavailable('Camera connection lost')}
  finally{polling=false;setTimeout(poll,750)}
}
setInterval(()=>{
  if(clock()-lastResponse>=2500){lastStatus=null;unavailable('Camera connection lost')}
  else if(lastStatus&&!sensorFresh(lastStatus)){cards(lastStatus,false);hideVideo('Waiting for fresh sensor data');text('state','Unavailable');text('temperature','—');text('fps','—')}
},250);

$('fullscreen').onclick=async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await document.documentElement.requestFullscreen();text('screenMessage','')}catch(e){text('screenMessage','Full screen is unavailable in this browser')}};
document.addEventListener('fullscreenchange',()=>text('fullscreen',document.fullscreenElement?'Exit full screen':'Full screen'));
poll();
</script></body></html>)THERMAL";
}
