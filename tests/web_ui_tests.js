// Run with Node: node tests/web_ui_tests.js. Uses the actual embedded scripts.
// The exported function can also run in another JS host after loading both headers.
async function testWebUi(settingsHeader,viewerHeader) {
  const assert=(ok,message)=>{if(!ok)throw Error(message)};
  const extract=header=>header.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements={},requests=[],timers=[],rectangles=[];
  const element=id=>elements[id]??=( {value:'',checked:false,textContent:'',style:{},hidden:false,
    getContext:()=>({clearRect(){},strokeRect(...args){rectangles.push(args)}}),
    getBoundingClientRect:()=>({left:0,top:0,width:320,height:240}),setPointerCapture(){},removeAttribute(key){delete this[key]}} );
  const config={roi:{x:7,y:11,width:13,height:9},flip_horizontal:true,palette:'ironbow',ha_discovery:true,
    delta_c:3,min_pixels:4,large_min_pixels:16,activate_ms:500,clear_ms:2000,learn_ms:10000,port:1883,
    ssid:'saved-network',broker:'',mqtt_user:'',topic:'thermal/test'};
  const status={frames:10,sensor_ready:true,frame_age_ms:0,video_enabled:true,sensor_error:'none',
    state:'learning',sensor_warmup_ms:5000,video_fps:6,min_c:20,max_c:30,small:{state:'learning',pixels:0},
    large:{state:'learning',pixels:0},wifi_connected:true,mqtt_configured:false,mqtt_connected:false,usb:'ready'};
  let failSave=false;
  const fetch=async(path,options={})=>{
    requests.push({path,options});
    if(options.method==='PUT')return {ok:!failSave,json:async()=>failSave?{error:'Rejected settings'}:{saved:true}};
    return {ok:true,json:async()=>path==='/api/config'?config:status};
  };
  const wait=async()=>{for(let i=0;i<15;++i)await Promise.resolve()};
  const document={getElementById:element};
  new Function('document','fetch','setTimeout',extract(settingsHeader))(document,fetch,fn=>timers.push(fn));
  await wait();
  assert(element('palette').value==='ironbow'&&element('ha_discovery').checked,'Saved palette/discovery not loaded');
  assert(element('wifi_password').value===''&&element('mqtt_password').value==='','Password field populated');
  assert(rectangles.at(-1)[0]===5+60*310/80,'Mirrored ROI overlay');
  const pointer=(x,y)=>({clientX:x,clientY:y,pointerId:1});
  element('roi').onpointerdown(pointer(5,0));element('roi').onpointerup(pointer(315,240));
  element('palette').value='black_hot';element('large_min_pixels').value='24';element('ha_discovery').checked=false;
  await element('settings').onsubmit({preventDefault(){}});
  let body=JSON.parse(requests.filter(r=>r.options.method==='PUT').at(-1).options.body);
  assert(body.palette==='black_hot'&&body.large_min_pixels===24&&body.ha_discovery===false,'New fields not saved');
  assert(body.roi.x===0&&body.roi.width===80&&body.roi.height===62,'Flipped full-image ROI mapping');
  assert(!('wifi_password' in body)&&!('mqtt_password' in body),'Blank passwords must be omitted');
  element('clearWifiPassword').checked=true;await element('settings').onsubmit({preventDefault(){}});
  body=JSON.parse(requests.filter(r=>r.options.method==='PUT').at(-1).options.body);
  assert(body.wifi_password==='','Explicit clear must send empty password');
  failSave=true;await element('settings').onsubmit({preventDefault(){}});
  assert(element('message').textContent==='Rejected settings'&&!element('save').disabled,'Failed save feedback');
  const viewerElements={},viewerTimers=[];
  const viewerDocument={getElementById:id=>viewerElements[id]??={style:{},removeAttribute(key){delete this[key]}},documentElement:{requestFullscreen:async()=>{}},exitFullscreen:async()=>{}};
  new Function('document','fetch','setTimeout',extract(viewerHeader))(viewerDocument,fetch,fn=>viewerTimers.push(fn));
  await wait();assert(viewerElements.feed.src.startsWith('/stream.mjpg'),'Viewer did not start video');
  status.sensor_ready=false;await viewerTimers.shift()();
  assert(!viewerElements.feed.src&&viewerElements.notice.style.display==='grid','Stale viewer image must be hidden');
  assert(!viewerHeader.includes('<form')&&viewerHeader.includes('href="/settings"'),'Viewer/settings split');
  return 'PASS: actual embedded JS loads/saves new settings, preserves passwords, maps flipped ROI, handles failures and hides stale video';
}
if(typeof module!=='undefined') {
  module.exports=testWebUi;
  if(require.main===module) {const fs=require('fs');testWebUi(fs.readFileSync('firmware/ThermalSecurityCamera/src/WebUi.h','utf8'),fs.readFileSync('firmware/ThermalSecurityCamera/src/ViewerUi.h','utf8')).then(console.log).catch(e=>{console.error(e);process.exitCode=1})}
}
