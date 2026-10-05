#pragma once
#include <Arduino.h>
namespace thermal {
static const char ViewerUi[] PROGMEM=R"THERMAL(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Thermal camera</title><style>
:root{color-scheme:dark;font:15px system-ui,sans-serif;background:#050608;color:#eef5fa}body{margin:0;height:100dvh;display:grid;place-items:center;overflow:hidden}#view{position:relative;width:min(100vw,133.333dvh);aspect-ratio:4/3}img{display:block;width:100%;height:100%;image-rendering:pixelated}#notice{position:absolute;inset:0;display:grid;place-content:center;text-align:center;background:#050608d9;padding:20px}nav{position:fixed;right:12px;top:12px;display:flex;gap:8px}a,button{font:inherit;color:#fff;background:#14202be6;border:1px solid #627789;border-radius:6px;padding:8px 12px;text-decoration:none;cursor:pointer}a:focus-visible,button:focus-visible{outline:2px solid #59daf4}
</style><main id="view"><img id="feed" alt="Live thermal camera"><div id="notice" role="status">Connecting to camera...</div></main><nav><button id="fullscreen" type="button">Full screen</button><a href="/settings">Settings</a></nav>
<script>
const feed=document.getElementById('feed'),notice=document.getElementById('notice');let started=false,nextStream=0,lastFrame=-1,frameAt=0;
feed.onerror=()=>{started=false;notice.style.display='grid';notice.textContent='Reconnecting video...'};
document.getElementById('fullscreen').onclick=async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await document.documentElement.requestFullscreen()}catch(e){notice.style.display='grid';notice.textContent='Full screen is unavailable in this browser'}};
async function poll(){try{const response=await fetch('/api/status',{cache:'no-store'});if(!response.ok)throw Error();const s=await response.json();if(lastFrame!==s.frames){lastFrame=s.frames;frameAt=Date.now()}const fresh=s.sensor_ready&&s.frame_age_ms<1000&&s.video_enabled&&Date.now()-frameAt<1500;if(fresh){notice.style.display='none';if(!started&&Date.now()>=nextStream){nextStream=Date.now()+5000;feed.src='/stream.mjpg?t='+Date.now();started=true}}else{feed.removeAttribute('src');started=false;notice.style.display='grid';notice.textContent=s.video_enabled===false?'Video paused':s.sensor_error==='none'?'Waiting for fresh video':s.sensor_error}}catch(e){feed.removeAttribute('src');started=false;notice.style.display='grid';notice.textContent='Camera connection lost'}setTimeout(poll,1000)}poll();
</script></html>)THERMAL";
}
