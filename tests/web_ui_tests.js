// Dependency-free tests against the scripts actually embedded in both pages.
// Run: node tests/web_ui_tests.js, or call the export from another JS host.
async function testWebUi(settingsHeader,viewerHeader) {
  const assert=(ok,message)=>{if(!ok)throw Error(message)};
  const config={roi:{x:7,y:11,width:13,height:9},flip_horizontal:true,palette:'ironbow',ha_discovery:true,
    delta_c:3,min_pixels:4,large_min_pixels:16,activate_ms:500,clear_ms:2000,learn_ms:10000,port:1883,
    ssid:'saved-network',broker:'',mqtt_user:'',topic:'thermal/test'};
  const baseline=()=>({frames:10,sensor_ready:true,frame_age_ms:0,video_enabled:true,sensor_error:'none',
    state:'clear',sensor_warmup_ms:0,video_fps:7.4,min_c:20,max_c:30,
    small:{state:'clear',pixels:0,peak_c:null},large:{state:'clear',pixels:0,peak_c:null},
    wifi_connected:true,mqtt_configured:false,mqtt_connected:false,usb:'ready'});
  async function harness(header){
    let time=100,timerId=0,status=baseline(),mode='ok',failSave=false,lateResolve=null;
    const elements={},timers=new Map(),requests=[],rectangles=[],listeners={};
    const ids=[...header.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
    assert(new Set(ids).size===ids.length,'Duplicate HTML element IDs');
    for(const id of ids){
      const el={value:'',checked:false,style:{},dataset:{},hidden:false,writes:0,
        getContext:()=>({clearRect(){},strokeRect(...args){rectangles.push(args)}}),
        getBoundingClientRect:()=>({left:0,top:0,width:320,height:248}),setPointerCapture(){},
        removeAttribute(key){delete this[key]}};
      let content='';Object.defineProperty(el,'textContent',{get:()=>content,set:v=>{content=v;el.writes++}});
      elements[id]=el;
    }
    const document={getElementById:id=>elements[id]||null,fullscreenElement:null,addEventListener:(name,fn)=>listeners[name]=fn,
      documentElement:{requestFullscreen:async()=>{document.fullscreenElement=document.documentElement}},
      exitFullscreen:async()=>{document.fullscreenElement=null}};
    const schedule=(fn,ms,repeat=false)=>{const id=++timerId;timers.set(id,{fn,at:time+ms,repeat:repeat?ms:0});return id};
    class AbortControllerMock{constructor(){this.signal={aborted:false}}abort(){this.signal.aborted=true}}
    class ThermalVideoMock {
      constructor(canvas,overlay,onState){this.running=false;this.onState=onState}
      start(){this.running=true;this.onState('')}
      stop(){this.running=false}
    }
    const fetch=async(path,options={})=>{
      requests.push({path,options});
      if(options.method==='PUT')return {ok:!failSave,statusText:'Bad Request',json:async()=>failSave?{error:'Rejected settings'}:{saved:true}};
      if(path==='/api/status'){
        if(mode==='reject')throw Error('Offline');
        if(mode==='hang')return new Promise(resolve=>{lateResolve=resolve});
        if(mode==='bad')return {ok:true,json:async()=>({})};
        const snapshot=JSON.parse(JSON.stringify(status));return {ok:true,json:async()=>snapshot};
      }
      return {ok:true,json:async()=>path==='/api/config'?JSON.parse(JSON.stringify(config)):{learning:true}};
    };
    const settle=async()=>{for(let n=0;n<30;n++)await Promise.resolve()};
    const code=header.match(/<script>([\s\S]*?)<\/script>/)[1];
    const api=new Function('document','fetch','setTimeout','clearTimeout','setInterval','performance','AbortController','ThermalVideo',
      code+'\nreturn {poll,video};')(document,fetch,(fn,ms)=>schedule(fn,ms),id=>timers.delete(id),(fn,ms)=>schedule(fn,ms,true),{now:()=>time},AbortControllerMock,ThermalVideoMock);
    await settle();
    async function advance(ms){
      const end=time+ms;let steps=0;
      while(true){let chosen=null;for(const [id,t]of timers)if(t.at<=end&&(!chosen||t.at<chosen[1].at))chosen=[id,t];
        if(!chosen)break;if(++steps>1000)throw Error('Timer runaway');
        const [id,t]=chosen;time=t.at;timers.delete(id);if(t.repeat)timers.set(id,{...t,at:time+t.repeat});
        t.fn();await settle();
      }time=end;await settle();
    }
    return {elements,requests,rectangles,document,listeners,api,advance,settle,
      get status(){return status},set status(v){status=v},set mode(v){mode=v},set failSave(v){failSave=v},
      resolveLate(){if(lateResolve)lateResolve({ok:true,json:async()=>baseline()})},
      next:async function(){status.frames++;await advance(750)}};
  }
  for(const [name,header]of [['viewer',viewerHeader],['settings',settingsHeader]]){
    const h=await harness(header),el=h.elements;
    const state=(channel)=>el[channel+'Panel'].dataset.state;
    assert(state('small')==='clear'&&state('large')==='clear',name+': initial clear states');
    assert(h.api.video.running,name+': video starts');
    h.status.small={state:'occupied',pixels:9,peak_c:31.25};h.status.state='occupied';await h.next();
    assert(state('small')==='occupied'&&state('large')==='clear',name+': small only');
    assert(el.smallState.textContent==='Detected'&&el.smallPixels.textContent==='9 pixels'&&el.smallPeak.textContent==='31.3 °C',name+': measurements');
    const announcements=el.smallState.writes;await h.next();
    assert(el.smallState.writes===announcements,name+': unchanged state must not be announced repeatedly');
    h.status.large={state:'occupied',pixels:25,peak_c:36};await h.next();
    assert(state('small')==='occupied'&&state('large')==='occupied',name+': simultaneous occupancy');
    h.status.small={state:'clear',pixels:0,peak_c:null};await h.next();
    assert(state('small')==='clear'&&state('large')==='occupied',name+': large only');
    h.status.large={state:'occupied',pixels:0,peak_c:36};await h.next();
    assert(state('large')==='occupied'&&el.largePixels.textContent==='—'&&el.largePeak.textContent==='—',name+': clear-delay measurements hidden');
    h.status.large={state:'occupied',pixels:20,peak_c:null};await h.next();
    assert(el.largePeak.textContent==='—'&&el.largePixels.textContent==='20 pixels',name+': missing temperature');
    h.status.video_enabled=false;await h.next();
    assert(state('large')==='occupied'&&!h.api.video.running&&el.stale.textContent==='Video paused',name+': detection independent of video');
    h.status.sensor_warmup_ms=5500;await h.next();
    assert(state('large')==='learning'&&el.largeState.textContent==='Warming up'&&el.largePeak.textContent==='—',name+': warming');
    h.status.sensor_warmup_ms=0;h.status.small.state=h.status.large.state=h.status.state='learning';await h.next();
    assert(el.smallState.textContent==='Learning'&&state('large')==='learning',name+': learning');
    h.status.sensor_ready=false;h.status.sensor_error='recovering';await h.next();
    assert(state('small')==='unavailable'&&state('large')==='unavailable',name+': sensor recovery');
    h.status=baseline();await h.next();assert(state('large')==='clear',name+': fresh stream recovers');
    h.status.frame_age_ms=1500;await h.next();assert(state('large')==='unavailable',name+': stale sensor age');
    h.status=baseline();h.status.small.state='occupied';await h.next();await h.advance(1750);
    assert(state('small')==='unavailable',name+': frozen frame counter');
    h.status=baseline();h.status.frames=100;await h.next();
    h.mode='reject';await h.next();assert(state('small')==='unavailable'&&!h.api.video.running,name+': failed request clears lights');
    h.mode='ok';h.status=baseline();h.status.frames=120;await h.next();assert(state('small')==='clear',name+': network recovery');
    h.mode='bad';await h.next();assert(state('large')==='unavailable',name+': malformed status');
    h.mode='ok';h.status=baseline();h.status.frames=140;h.status.small.state='occupied';await h.next();
    h.mode='hang';await h.next();const requestCount=h.requests.filter(r=>r.path==='/api/status').length;
    await h.api.poll();await h.advance(1800);
    assert(h.requests.filter(r=>r.path==='/api/status').length===requestCount,name+': one status request at a time');
    assert(state('small')==='unavailable',name+': local watchdog clears green during hung fetch');
    await h.advance(800);
    const hung=h.requests.filter(r=>r.path==='/api/status').at(-1);
    assert(hung.options.signal.aborted,name+': hung status request aborted');
    h.resolveLate();await h.settle();assert(state('small')==='unavailable',name+': late response cannot revive stale lights');
    if(name==='viewer'){
      await el.fullscreen.onclick();h.listeners.fullscreenchange();
      assert(h.document.fullscreenElement===h.document.documentElement&&el.fullscreen.textContent==='Exit full screen','Fullscreen includes dashboard');
      await el.fullscreen.onclick();h.listeners.fullscreenchange();assert(!h.document.fullscreenElement,'Exit fullscreen');
    }
  }
  const h=await harness(settingsHeader),el=h.elements;
  assert(el.palette.value==='ironbow'&&el.ha_discovery.checked,'Saved palette/discovery loaded');
  assert(el.wifi_password.value===''&&el.mqtt_password.value==='','Passwords not populated');
  assert(h.rectangles.at(-1)[0]===60*8,'Mirrored ROI overlay');
  const pointer=(x,y)=>({clientX:x,clientY:y,pointerId:1});
  el.roi.onpointerdown(pointer(0,0));el.roi.onpointerup(pointer(320,248));
  el.palette.value='black_hot';el.large_min_pixels.value='24';el.ha_discovery.checked=false;
  await el.settings.onsubmit({preventDefault(){}});
  let body=JSON.parse(h.requests.filter(r=>r.options.method==='PUT').at(-1).options.body);
  assert(body.palette==='black_hot'&&body.large_min_pixels===24&&body.ha_discovery===false,'Settings fields saved');
  assert(body.roi.x===0&&body.roi.width===80&&body.roi.height===62,'Flipped full-image ROI mapping');
  assert(!('wifi_password'in body)&&!('mqtt_password'in body),'Blank passwords omitted');
  el.clearWifiPassword.checked=true;await el.settings.onsubmit({preventDefault(){}});
  body=JSON.parse(h.requests.filter(r=>r.options.method==='PUT').at(-1).options.body);
  assert(body.wifi_password==='','Explicit password clear');
  h.failSave=true;await el.settings.onsubmit({preventDefault(){}});
  assert(el.message.textContent==='Rejected settings'&&!el.save.disabled,'Failed save feedback');
  await el.learn.onclick();assert(h.requests.at(-1).path==='/api/relearn','Relearn action preserved');
  assert(!viewerHeader.includes('<form')&&viewerHeader.includes('href="/settings"'),'Viewer/settings split');
  assert(viewerHeader.includes('prefers-reduced-motion')&&settingsHeader.includes('prefers-reduced-motion'),'Reduced motion');
  return 'PASS: both embedded pages: detection states, measurements, video pause, watchdog, timeout, recovery, single-flight polling, fullscreen, settings, passwords and mirrored ROI';
}
if(typeof module!=='undefined'){
  module.exports=testWebUi;
  if(require.main===module){const fs=require('fs');testWebUi(fs.readFileSync('firmware/ThermalSecurityCamera/src/WebUi.h','utf8'),fs.readFileSync('firmware/ThermalSecurityCamera/src/ViewerUi.h','utf8')).then(console.log).catch(e=>{console.error(e);process.exitCode=1})}
}
