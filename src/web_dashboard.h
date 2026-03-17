#pragma once

// ============================================================================
// Embedded HTML/CSS/JS Dashboard
// Single-page motor control dashboard with rolling RPM chart
// ============================================================================

static const char DASHBOARD_HTML[] = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Motor Control</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f1117;color:#e0e0e0;padding:12px}
h1{font-size:1.3em;color:#7eb8ff;margin-bottom:4px}
.sub{font-size:.8em;color:#888;margin-bottom:12px}
.grid{display:grid;gap:12px;margin-bottom:12px}
.g2{grid-template-columns:1fr 1fr}
.card{background:#1a1d27;border:1px solid #2a2d3a;border-radius:8px;padding:14px}
.card h2{font-size:1em;color:#9ba4b5;margin-bottom:10px;border-bottom:1px solid #2a2d3a;padding-bottom:6px}
label{font-size:.8em;color:#777;display:block;margin-bottom:2px}
.val{font-size:1.4em;font-weight:700;color:#fff;margin-bottom:8px}
.val.ok{color:#4ade80}.val.warn{color:#fbbf24}.val.err{color:#f87171}
.row{display:flex;gap:8px;align-items:center;margin-bottom:8px;flex-wrap:wrap}
input[type=number]{background:#12141c;border:1px solid #333;color:#fff;border-radius:4px;padding:6px 8px;width:90px;font-size:.95em}
select{background:#12141c;border:1px solid #333;color:#fff;border-radius:4px;padding:6px;font-size:.9em}
button{border:none;border-radius:4px;padding:8px 14px;font-size:.85em;font-weight:600;cursor:pointer;transition:opacity .15s}
button:hover{opacity:.85}
.btn-go{background:#22c55e;color:#000}.btn-stop{background:#ef4444;color:#fff}
.btn-brake{background:#f59e0b;color:#000}.btn-coast{background:#3b82f6;color:#fff}
.btn-dir{background:#8b5cf6;color:#fff}.btn-sm{padding:5px 10px;font-size:.8em}
.tag{display:inline-block;padding:2px 8px;border-radius:3px;font-size:.75em;font-weight:600}
.tag-run{background:#166534;color:#4ade80}.tag-idle{background:#1e3a5f;color:#60a5fa}
.tag-stall{background:#7f1d1d;color:#fca5a5}.tag-brake{background:#78350f;color:#fcd34d}
.tag-coast{background:#1e3a5f;color:#93c5fd}
canvas{width:100%;height:200px;background:#12141c;border-radius:6px;border:1px solid #2a2d3a}
.sys{display:flex;gap:16px;flex-wrap:wrap;font-size:.82em;color:#888}
.sys span{color:#bbb}
.legend{display:flex;gap:16px;font-size:.8em;margin-top:6px}
.legend i{display:inline-block;width:12px;height:3px;border-radius:2px;margin-right:4px;vertical-align:middle}
@media(max-width:600px){.g2{grid-template-columns:1fr}}
</style>
</head>
<body>
<h1>BLDC Axis Drive System</h1>
<div class="sub" id="ssid">Connecting...</div>

<!-- System Info -->
<div class="card" style="margin-bottom:12px">
 <div class="sys">
  <div>Uptime: <span id="uptime">--</span></div>
  <div>Heap: <span id="heap">--</span></div>
  <div>Status: <span id="sys-status">--</span></div>
 </div>
</div>

<!-- Motor Cards -->
<div class="grid g2" id="motors"></div>

<!-- Controls -->
<div class="card" style="margin-bottom:12px">
 <h2>Controls</h2>
 <div class="row">
  <label style="display:inline;margin-right:4px">Target:</label>
  <select id="ctrl-motor"><option value="all">All Motors</option></select>
  <input type="number" id="ctrl-rpm" value="0" min="0" max="2000" step="10" placeholder="RPM">
  <button class="btn-go" onclick="cmdSet()">Set RPM</button>
 </div>
 <div class="row">
  <button class="btn-dir" onclick="cmdDir('fwd')">Forward</button>
  <button class="btn-dir" onclick="cmdDir('rev')">Reverse</button>
  <button class="btn-go" onclick="cmd('start')">Start</button>
  <button class="btn-stop" onclick="cmd('stop')">E-STOP</button>
  <button class="btn-brake" onclick="cmd('brake')">Brake</button>
  <button class="btn-coast" onclick="cmd('coast')">Coast</button>
 </div>
</div>

<!-- RPM Chart -->
<div class="card">
 <h2>RPM History (60s)</h2>
 <canvas id="chart" height="200"></canvas>
 <div class="legend" id="legend"></div>
</div>

<script>
const POLL_MS=500, HIST_LEN=120;
const COLORS=['#22c55e','#3b82f6','#f59e0b','#ef4444'];
let hist=[], motorCount=0, chartCtx=null;

function $(s){return document.getElementById(s)}
function fmt(n){return n<10?'0'+n:''+n}
function fmtTime(ms){
 let s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
 return fmt(h)+':'+fmt(m%60)+':'+fmt(s%60);
}

function initMotorCards(n){
 if(motorCount===n)return;
 motorCount=n;
 let mc=$('motors'), sel=$('ctrl-motor');
 mc.innerHTML=''; sel.innerHTML='<option value="all">All Motors</option>';
 for(let i=0;i<n;i++){
  sel.innerHTML+='<option value="'+i+'">Motor '+i+'</option>';
  let c=document.createElement('div');
  c.className='card'; c.id='m'+i;
  c.innerHTML='<h2>Motor '+i+' <span class="tag tag-idle" id="m'+i+'-tag">IDLE</span></h2>'
   +'<div class="row"><div><label>RPM</label><div class="val" id="m'+i+'-rpm">0</div></div>'
   +'<div><label>Target</label><div class="val" id="m'+i+'-trpm" style="color:#9ba4b5">0</div></div>'
   +'<div><label>Duty</label><div class="val" id="m'+i+'-duty">0%</div></div>'
   +'<div><label>Dir</label><div class="val" id="m'+i+'-dir" style="font-size:1em">FWD</div></div></div>';
  mc.appendChild(c);
 }
 // legend
 let lg=$('legend'); lg.innerHTML='';
 for(let i=0;i<n;i++){
  lg.innerHTML+='<div><i style="background:'+COLORS[i]+'"></i>M'+i+'</div>';
 }
 hist=[];
}

function updateUI(d){
 $('uptime').textContent=fmtTime(d.uptime_ms);
 $('heap').textContent=Math.round(d.heap_free/1024)+' KB';
 $('sys-status').innerHTML=d.running
  ?'<span class="tag tag-run">RUNNING</span>'
  :'<span class="tag tag-idle">IDLE</span>';

 initMotorCards(d.motors.length);

 let pt={t:Date.now(),rpm:[]};
 for(let i=0;i<d.motors.length;i++){
  let m=d.motors[i], p='m'+i;
  $(p+'-rpm').textContent=m.rpm.toFixed(1);
  $(p+'-rpm').className='val'+(m.stalled?' err':m.rpm>0?' ok':'');
  $(p+'-trpm').textContent=m.target_rpm.toFixed(0);
  $(p+'-duty').textContent=m.duty_pct+'%';
  $(p+'-dir').textContent=m.dir==='fwd'?'FWD':'REV';
  let tag=$(p+'-tag');
  if(m.stalled){tag.className='tag tag-stall';tag.textContent='STALL';}
  else if(m.braking){tag.className='tag tag-brake';tag.textContent='BRAKE';}
  else if(m.coasting){tag.className='tag tag-coast';tag.textContent='COAST';}
  else if(m.rpm>0){tag.className='tag tag-run';tag.textContent='RUN';}
  else{tag.className='tag tag-idle';tag.textContent='IDLE';}
  pt.rpm.push(m.rpm);
 }
 hist.push(pt);
 if(hist.length>HIST_LEN)hist.shift();
 drawChart();
}

function drawChart(){
 let cv=$('chart'), ctx=chartCtx;
 if(!ctx){cv.width=cv.offsetWidth;cv.height=200;ctx=chartCtx=cv.getContext('2d');}
 let W=cv.width, H=cv.height;
 ctx.clearRect(0,0,W,H);
 if(hist.length<2)return;

 // find max RPM for Y scale
 let maxR=10;
 for(let p of hist) for(let r of p.rpm) if(r>maxR) maxR=r;
 maxR=Math.ceil(maxR/10)*10+10;

 let padL=45,padB=20,padT=10,padR=10;
 let gW=W-padL-padR, gH=H-padT-padB;

 // grid lines
 ctx.strokeStyle='#2a2d3a'; ctx.lineWidth=1;
 ctx.font='10px sans-serif'; ctx.fillStyle='#555';
 let steps=5;
 for(let i=0;i<=steps;i++){
  let y=padT+gH-(i/steps)*gH;
  let v=Math.round((i/steps)*maxR);
  ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(W-padR,y);ctx.stroke();
  ctx.fillText(v,4,y+3);
 }

 // draw lines per motor
 let n=hist[0].rpm.length;
 for(let m=0;m<n;m++){
  ctx.strokeStyle=COLORS[m]; ctx.lineWidth=2;
  ctx.beginPath();
  for(let i=0;i<hist.length;i++){
   let x=padL+(i/(HIST_LEN-1))*gW;
   let y=padT+gH-(hist[i].rpm[m]/maxR)*gH;
   if(i===0)ctx.moveTo(x,y); else ctx.lineTo(x,y);
  }
  ctx.stroke();
 }

 // time labels
 ctx.fillStyle='#555';
 let ago=Math.round((hist.length-1)*POLL_MS/1000);
 ctx.fillText('-'+ago+'s',padL,H-3);
 ctx.fillText('now',W-padR-20,H-3);
}

async function poll(){
 try{
  let r=await fetch('/api/status');
  let d=await r.json();
  updateUI(d);
  $('ssid').textContent='Connected';
 }catch(e){
  $('ssid').textContent='Connection lost...';
 }
}

function cmd(ep){
 let m=$('ctrl-motor').value;
 let q=m!=='all'?'?motor='+m:'';
 fetch('/api/'+ep+q);
}
function cmdSet(){
 let m=$('ctrl-motor').value;
 let rpm=$('ctrl-rpm').value;
 let q='?rpm='+rpm;
 if(m!=='all')q+='&motor='+m;
 fetch('/api/set'+q);
}
function cmdDir(d){
 let m=$('ctrl-motor').value;
 let q='?dir='+d;
 if(m!=='all')q+='&motor='+m;
 fetch('/api/dir'+q);
}

// Handle chart resize
window.addEventListener('resize',()=>{
 chartCtx=null;
 let cv=$('chart');cv.width=cv.offsetWidth;cv.height=200;
 drawChart();
});

setInterval(poll,POLL_MS);
poll();
</script>
</body>
</html>
)rawhtml";
