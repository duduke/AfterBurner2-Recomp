import {SceneRenderer,decodeBytes} from './scene.js';
const $=id=>document.getElementById(id);
const canvas=$('screen'),view=new SceneRenderer(canvas),keys=new Set();
let paused=false,busy=false,operationPending=false,stopped=false,coin=0,start=0,currentFrame=0;
let needFullScene=true;
let audioContext,audioHead=0,frameHistory=[],checkedFrames=0,mismatches=0;
const notice=text=>{$('notice').textContent=text;};
async function request(body){
  const response=await fetch('/api/frame',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...body,fullScene:needFullScene,verify:$('verify').checked})});
  const frame=await response.json();if(!response.ok)throw new Error(frame.error??'Game bridge failed');if(body.action==='tick')needFullScene=false;return frame;
}
function playAudio(data){
  if(!$('audio').checked||!audioContext||audioContext.state!=='running')return;
  const bytes=decodeBytes(data),samples=new DataView(bytes.buffer);if(!bytes.length)return;
  const buffer=audioContext.createBuffer(2,bytes.length/4,48000);
  for(let i=0;i<buffer.length;i++){buffer.getChannelData(0)[i]=samples.getInt16(i*4,true)/32768;buffer.getChannelData(1)[i]=samples.getInt16(i*4+2,true)/32768;}
  const source=audioContext.createBufferSource();source.buffer=buffer;source.connect(audioContext.destination);
  audioHead=Math.max(audioContext.currentTime+.02,audioHead);source.start(audioHead);audioHead+=buffer.duration;
}
function display(frame){
  const began=performance.now();
  currentFrame=frame.frame;view.render(frame);playAudio(frame.audio);
  $('cost').textContent=`${(frame.performance?.coreMs??0).toFixed(1)} / ${(frame.performance?.captureMs??0).toFixed(1)} / ${(performance.now()-began).toFixed(1)} ms`;
  if(view.lastStats.matchesNative!==undefined){++checkedFrames;if(!view.lastStats.matchesNative)++mismatches;}
  $('validation').textContent=`${checkedFrames} rendered-frame checks · ${mismatches} mismatches`;
  $('match').textContent=view.lastStats.matchesNative===undefined?'OFF':view.lastStats.matchesNative?'MATCH':'DIFF';
  $('frame').textContent=frame.frame;$('objects').textContent=view.lastStats.objects;$('strips').textContent=view.lastStats.strips;
  const now=performance.now();frameHistory.push(now);frameHistory=frameHistory.filter(time=>now-time<2000);
  $('fps').textContent=frameHistory.length>1?((frameHistory.length-1)*1000/(now-frameHistory[0])).toFixed(1):'—';
  window.prototypeState={frame:frame.frame,...view.lastStats,paused};
}
async function resetAudio(){if(audioContext){await audioContext.close();audioContext=undefined;}audioHead=0;if($('audio').checked){audioContext=new AudioContext({sampleRate:48000});await audioContext.resume();}}
function togglePause(){if(stopped)return;paused=!paused;keys.clear();$('pause').textContent=paused?'P · Resume':'P · Pause';notice(paused?'Paused':'');if(audioContext){if(paused)audioContext.suspend();else audioContext.resume();}}
canvas.addEventListener('keydown',event=>{
  if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Space','ShiftLeft','KeyZ','KeyX','Digit5','Enter','KeyP'].includes(event.code))event.preventDefault();
  keys.add(event.code);if(event.code==='KeyP'&&!event.repeat)togglePause();
});
window.addEventListener('keyup',event=>keys.delete(event.code));window.addEventListener('blur',()=>keys.clear());
$('coin').onclick=()=>{coin=12;canvas.focus();};$('start').onclick=()=>{start=12;canvas.focus();};$('pause').onclick=()=>{togglePause();canvas.focus();};
$('verify').onchange=()=>{view.verify=$('verify').checked;};
$('wireframe').onchange=()=>{view.wireframe=$('wireframe').checked;view.presentFrame();};
$('audio').onchange=async()=>{try{await resetAudio();}catch(error){notice(error.message);}};
async function exclusive(action){
  if(operationPending)return;operationPending=true;
  while(busy)await new Promise(resolve=>setTimeout(resolve,10));
  busy=true;for(const button of document.querySelectorAll('button'))button.disabled=true;
  try{await action();}catch(error){stopped=true;notice(error.message);}finally{busy=false;operationPending=false;for(const button of document.querySelectorAll('button'))button.disabled=false;canvas.focus();}
}
$('restart').onclick=()=>exclusive(async()=>{await resetAudio();display(await request({action:'restart'}));stopped=false;paused=false;coin=start=0;keys.clear();$('pause').textContent='P · Pause';notice('');});
$('boot').onclick=()=>exclusive(async()=>{
  notice('Executing the original boot and coin/start sequence…');
  await resetAudio();await request({action:'restart'});frameHistory=[];display(await request({action:'demo',count:600}));
  paused=true;stopped=false;coin=start=0;keys.clear();$('pause').textContent='P · Resume';notice('Ready to fly · press P to resume');
  if(audioContext)await audioContext.suspend();
});
let nextTick=performance.now();
async function loop(){
  const began=performance.now();let completed;
  if(!busy&&!operationPending&&!paused&&!stopped){
    busy=true;
    try{
      completed=await request({action:'tick',x:keys.has('ArrowRight')?1:keys.has('ArrowLeft')?-1:0,y:keys.has('ArrowDown')?-1:keys.has('ArrowUp')?1:0,
        throttle:keys.has('ShiftLeft')?-1:keys.has('Space')?1:0,cannon:keys.has('KeyZ'),missile:keys.has('KeyX'),coin:coin>0||keys.has('Digit5'),start:start>0||keys.has('Enter')});
      if(coin>0)--coin;if(start>0)--start;
    }catch(error){stopped=true;notice(error.message);}finally{busy=false;}
  }
  // Use a wall-clock deadline so timer quantization does not accumulate drift.
  nextTick=Math.max(nextTick+16.768,began);
  const delay=nextTick-performance.now();
  // Nested browser timers have a 4 ms floor. Start the asynchronous request
  // within that floor and let cumulative deadlines pace the following frame;
  // computing up to 4 ms ahead avoids systematically missing the board rate.
  if(delay>4)setTimeout(loop,delay);else loop();
  // Start the next native request before GPU submission so both can overlap.
  // Every completed board frame is still rendered, in order, with no dropping.
  if(completed){
    try{display(completed);
      if(currentFrame===1)notice('Booting · use Boot to flight to advance through the startup sequence');
      if(currentFrame>180&&!paused)notice('');
    }catch(error){stopped=true;notice(error.message);}
  }
}
new ResizeObserver(()=>view.presentFrame()).observe(canvas);
// Presentation follows the monitor independently of physical board frames.
// 120 Hz repeats the latest faithful state; it does not speed up ROM gameplay.
let displayStart=performance.now(),displayFrames=0;
function present(now){
  view.presentFrame();++displayFrames;
  if(now-displayStart>=1000){$('displayFps').textContent=(displayFrames*1000/(now-displayStart)).toFixed(1);displayFrames=0;displayStart=now;}
  requestAnimationFrame(present);
}
canvas.focus();loop();requestAnimationFrame(present);
