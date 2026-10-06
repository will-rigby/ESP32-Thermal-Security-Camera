#pragma once
#include <Arduino.h>
namespace thermal {
static const char VideoClientUi[] PROGMEM=R"THERMAL(
class ThermalVideo {
  constructor(canvas,overlay,onState,onFrame=()=>{}) {
    this.canvas=canvas;this.context=canvas.getContext('2d');
    this.overlay=overlay;this.boxes=overlay.getContext('2d');
    this.image=this.context.createImageData(80,62);
    this.onState=onState;this.onFrame=onFrame;this.running=false;this.socket=null;
    this.timer=0;this.deadline=0;this.retry=0;this.palette=-1;this.lut=null;
    this.drawRoi=!document.getElementById('roi');
  }
  start() { if(this.running)return;this.running=true;this.connect(); }
  stop() {
    this.running=false;clearTimeout(this.timer);clearTimeout(this.deadline);clearTimeout(this.retry);
    const ws=this.socket;this.socket=null;if(ws)ws.close();
    this.boxes.clearRect(0,0,640,496);
  }
  connect() {
    if(!this.running)return;
    this.onState('Connecting video…');this.lastSerial=null;this.frameAt=performance.now();
    const ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/stream.yuy2');
    this.socket=ws;ws.binaryType='arraybuffer';
    this.deadline=setTimeout(()=>this.fail(ws),1500);
    ws.onopen=()=>{if(this.socket!==ws)return;clearTimeout(this.deadline);this.ask(ws)};
    ws.onerror=()=>this.fail(ws);ws.onclose=()=>this.fail(ws);
    ws.onmessage=e=>{
      if(this.socket!==ws)return;
      clearTimeout(this.deadline);
      try {
        if(!(e.data instanceof ArrayBuffer))throw Error('Invalid video message');
        if(e.data.byteLength) {
          const frame=this.decode(e.data);
          if(frame.serial!==this.lastSerial) {
            this.lastSerial=frame.serial;this.frameAt=performance.now();
            this.draw(frame);this.onFrame(frame);this.onState('');
          }
        }
        if(performance.now()-this.frameAt>=1500)throw Error('Video stalled');
        // One request in flight; a slow connection never accumulates frames.
        this.timer=setTimeout(()=>this.ask(ws),Math.max(1,50-(performance.now()-this.requestAt)));
      } catch(e) { this.fail(ws); }
    };
  }
  fail(ws) {
    if(this.socket!==ws)return;
    this.socket=null;clearTimeout(this.timer);clearTimeout(this.deadline);ws.close();
    this.boxes.clearRect(0,0,640,496);this.onState('Reconnecting video…');
    if(this.running)this.retry=setTimeout(()=>this.connect(),1000);
  }
  ask(ws) {
    if(this.socket!==ws || ws.readyState!==1)return;
    this.requestAt=performance.now();this.deadline=setTimeout(()=>this.fail(ws),1500);
    ws.send(new Uint8Array([1]));
  }
  decode(buffer) {
    if(buffer.byteLength!==9984)throw Error('Invalid frame length');
    const v=new DataView(buffer),bytes=new Uint8Array(buffer);
    if(v.getUint32(0,true)!==0x32595559 || v.getUint16(4,true)!==1 || v.getUint16(6,true)!==64 ||
       v.getUint16(8,true)!==80 || v.getUint16(10,true)!==62 || bytes[20]>1 || bytes[21]>4 || bytes[22]>3)
      throw Error('Unsupported video format');
    const rect=offset=>{const r={x:v.getUint16(offset,true),y:v.getUint16(offset+2,true),width:v.getUint16(offset+4,true),height:v.getUint16(offset+6,true)};
      if(r.x+r.width>80 || r.y+r.height>62)throw Error('Invalid bounds');return r};
    return {bytes,serial:v.getUint32(12,true),frameMs:v.getUint32(16,true),flip:!!bytes[20],palette:bytes[21],
      roi:rect(32),small:rect(40),large:rect(48),smallPixels:v.getUint16(56,true),largePixels:v.getUint16(58,true)};
  }
  makeLut(palette) {
    const stops=[[[0,0,0],[255,0,0],[255,255,0],[255,255,255]],
      [[0,0,0],[35,0,85],[120,15,110],[220,55,45],[255,165,30],[255,255,225]],
      [[0,0,100],[0,80,255],[0,240,255],[0,220,40],[255,240,0],[255,0,0]]][palette];
    this.lut=new Uint8ClampedArray(256*3);this.palette=palette;
    for(let i=0;i<256;i++) {
      let colour=[i,i,i];
      if(stops) {
        const scaled=i*(stops.length-1),segment=Math.floor(scaled/255),fraction=scaled%255;
        colour=segment===stops.length-1?stops[segment]:stops[segment].map((c,k)=>Math.floor((c*(255-fraction)+stops[segment+1][k]*fraction)/255));
      }
      this.lut.set(colour,i*3);
    }
  }
  draw(frame) {
    if(this.palette!==frame.palette)this.makeLut(frame.palette);
    for(let i=0;i<4960;i++) {
      // Both outputs are grayscale YUY2; browser palettes are applied locally.
      const level=Math.max(0,Math.min(255,Math.round((frame.bytes[64+i*2]-16)*255/219)));
      const at=i*4,colour=level*3;
      this.image.data[at]=this.lut[colour];this.image.data[at+1]=this.lut[colour+1];
      this.image.data[at+2]=this.lut[colour+2];this.image.data[at+3]=255;
    }
    this.context.putImageData(this.image,0,0);
    this.boxes.clearRect(0,0,640,496);this.boxes.lineWidth=1.5;
    const box=(r,colour)=>{
      if(!r.width || !r.height)return;
      const x=frame.flip?80-r.x-r.width:r.x;
      this.boxes.strokeStyle=colour;this.boxes.strokeRect(x*8,r.y*8,r.width*8,r.height*8);
    };
    if(this.drawRoi)box(frame.roi,'#53dfff');
    if(frame.smallPixels)box(frame.small,'#80ff80');
    if(frame.largePixels)box(frame.large,'#80ff80');
  }
}
)THERMAL";
}
