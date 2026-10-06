// Test the actual browser decoder against a frame serialized by C++.
// Run scripts/test.ps1 first to create .cache/tests/gray-frame.yuy2.
const fs=require('fs'),assert=require('assert');
const header=fs.readFileSync('firmware/ThermalSecurityCamera/src/VideoClientUi.h','utf8');
const code=header.match(/R"THERMAL\(([\s\S]*?)\)THERMAL"/)[1];
const fixture=fs.readFileSync('.cache/tests/gray-frame.yuy2');
const packet=()=>fixture.buffer.slice(fixture.byteOffset,fixture.byteOffset+fixture.byteLength);
let time=0,nextTimer=0,editing=false;
const timers=new Map(),sockets=[],boxes=[],states=[],frames=[];
const context={createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4)}),putImageData(image){this.last=image.data.slice()}};
const overlay={clearRect(){boxes.length=0},strokeRect(...r){boxes.push(r)}};
class Socket {
  constructor(url){this.url=url;this.readyState=0;this.sent=[];this.closed=false;sockets.push(this)}
  send(data){assert.strictEqual(this.readyState,1);this.sent.push(data)}
  close(){this.closed=true;this.readyState=3}
  open(){this.readyState=1;this.onopen()}
  receive(data){this.onmessage({data})}
}
const schedule=(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id};
const Video=new Function('document','location','WebSocket','setTimeout','clearTimeout','performance',code+'\nreturn ThermalVideo;')(
  {getElementById:()=>editing?{}:null},{protocol:'http:',host:'camera.local'},Socket,schedule,id=>timers.delete(id),{now:()=>time});
const video=new Video({getContext:()=>context},{getContext:()=>overlay},s=>states.push(s),f=>frames.push(f));
video.start();video.start();assert.strictEqual(sockets.length,1,'Idempotent start');
const ws=sockets[0];assert.strictEqual(ws.url,'ws://camera.local/stream.yuy2');ws.open();
assert.deepStrictEqual([...ws.sent[0]],[1]);assert.strictEqual(ws.sent.length,1,'Only one initial request');
ws.receive(packet());
assert.strictEqual(frames.length,1);assert.strictEqual(frames[0].serial,42);
assert.strictEqual(context.last[0],0);assert.strictEqual(context.last[4],255);
for(let i=0;i<4960;i++)assert.strictEqual(context.last[i*4+3],255);
assert.deepStrictEqual(boxes,[[0,0,640,496],[16,24,32,40],[320,80,64,64]],'Native bounds enlarged on overlay');
assert.strictEqual(states.at(-1),'');assert.strictEqual(ws.sent.length,1,'No request until next timer');
time=50;timers.get(video.timer).fn();assert.strictEqual(ws.sent.length,2);
ws.receive(new ArrayBuffer(0));assert.strictEqual(frames.length,1,'Empty reply does not redraw');
time=100;timers.get(video.timer).fn();ws.receive(packet());assert.strictEqual(frames.length,1,'Duplicate serial does not revive video');
const flipped=packet(),fv=new DataView(flipped);fv.setUint32(12,43,true);fv.setUint8(20,1);
time=150;timers.get(video.timer).fn();ws.receive(flipped);
assert.deepStrictEqual(boxes[1],[592,24,32,40],'Mirrored small region');
for(let palette=0;palette<5;palette++) {
  const value=packet(),v=new DataView(value);v.setUint8(21,palette);v.setUint32(12,50+palette,true);
  ws.receive(value);assert.strictEqual(video.palette,palette);
}
const rainbow=packet();new DataView(rainbow).setUint8(21,2);video.draw(video.decode(rainbow));
assert.deepStrictEqual([...context.last.slice(0,3)],[0,0,100]);
assert.throws(()=>video.decode(packet().slice(0,100)));
for(const [offset,value] of [[0,0],[4,2],[6,63],[8,81],[10,63],[20,2],[21,5],[22,4],[32,80]]) {
  const corrupt=packet();new Uint8Array(corrupt)[offset]=value;
  assert.throws(()=>video.decode(corrupt),'Reject bad protocol field '+offset);
}
ws.receive(new ArrayBuffer(2));assert(ws.closed);assert.strictEqual(states.at(-1),'Reconnecting video…');
timers.get(video.retry).fn();const next=sockets.at(-1);next.open();
ws.receive(packet());assert.strictEqual(video.socket,next,'Late old socket ignored');
timers.get(video.deadline).fn();assert(next.closed,'Hung request times out');
timers.get(video.retry).fn();const stalled=sockets.at(-1);stalled.open();
time+=1600;stalled.receive(new ArrayBuffer(0));assert(stalled.closed,'No fresh frames triggers reconnect');
video.stop();assert.strictEqual(video.running,false);assert.strictEqual(video.socket,null);
editing=true;const settings=new Video({getContext:()=>context},{getContext:()=>overlay},()=>{});
settings.draw(settings.decode(packet()));assert.strictEqual(boxes.length,2,'Settings ROI belongs to editable overlay');
console.log('PASS: C++/browser protocol, grayscale and palettes, frame-synchronous thin boxes, mirroring, pull pacing, malformed frames, timeout, reconnect and stale-socket isolation');
