// Control page, served from flash. Single file, no external assets.
#pragma once

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#e8641f"><meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-title" content="Door Sign">
<title>Door Sign</title>
<style>
:root{--bg:#f6f5f2;--card:#fff;--ink:#1b1b1b;--muted:#6b6b6b;--line:#e2e0db;--accent:#e8641f;--red:#c0182b;--black:#1b1b1b}
@media (prefers-color-scheme:dark){:root{--bg:#141414;--card:#1f1f1f;--ink:#f1f1f1;--muted:#a0a0a0;--line:#333;--black:#000}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:14px 16px;border-bottom:3px solid var(--accent);background:var(--card)}
header h1{font-size:18px;margin:0;flex:1}nav button{background:none;border:0;color:var(--muted);font:inherit;padding:6px 10px;cursor:pointer;border-radius:6px}
nav button.on{color:var(--ink);background:var(--bg);font-weight:600}
main{max-width:720px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
.sign{display:block;width:100%;height:auto;aspect-ratio:5/3;border-radius:6px;border:8px solid #d9d6cf;background:#fff;image-rendering:auto}
.preview{border-radius:8px;padding:28px 12px;text-align:center;font-weight:800;font-size:clamp(24px,7vw,40px);letter-spacing:.04em;border:1px solid var(--line)}
.preview.red{background:var(--red);color:#fff}.preview.black{background:var(--black);color:#fff}.preview.none{background:#fff;color:#111}
.sub{text-align:center;color:var(--muted);margin-top:10px;min-height:1.4em}.quip{font-style:italic}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.st{padding:18px 8px;border-radius:8px;border:2px solid var(--line);background:var(--card);color:var(--ink);font:600 16px system-ui,sans-serif;cursor:pointer}
.st.red{border-left:8px solid var(--red)}.st.black{border-left:8px solid var(--black)}.st:active{transform:scale(.98)}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin:4px 0 0}.chip{padding:6px 12px;border-radius:999px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer;font:14px system-ui,sans-serif}
.chip.on{background:var(--accent);border-color:var(--accent);color:#fff}
h2{font-size:15px;margin:0 0 10px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
label{display:block;font-size:14px;color:var(--muted);margin:10px 0 4px}
input,select{width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif}
.row{display:flex;gap:10px}.row>*{flex:1}
.btn{padding:12px 16px;border-radius:8px;border:0;background:var(--accent);color:#fff;font:600 16px system-ui,sans-serif;cursor:pointer}
.btn.ghost{background:transparent;color:var(--ink);border:1px solid var(--line)}
.days{display:flex;gap:6px;flex-wrap:wrap}.days label{display:flex;align-items:center;gap:4px;margin:0;color:var(--ink)}.days input{width:auto}
table{width:100%;border-collapse:collapse;font-size:14px}td{padding:6px 0;border-bottom:1px solid var(--line)}td:first-child{color:var(--muted);width:40%}
pre{white-space:pre-wrap;font-size:12px;max-height:300px;overflow:auto;background:var(--bg);padding:10px;border-radius:8px}
.moods{display:grid;grid-template-columns:repeat(auto-fill,minmax(84px,1fr));gap:8px}
.mood{padding:8px 4px;border-radius:8px;border:2px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer;font:12px system-ui,sans-serif;text-align:center}
.mood b{display:block;font-size:28px;line-height:1.2;font-weight:400}.mood.on{border-color:var(--accent);background:var(--bg)}
.cal{display:flex;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid var(--line)}.cal input{width:auto}.cal span{flex:1}
.cal .dot{width:12px;height:12px;border-radius:50%;flex:none}.cal select{width:auto;padding:6px}
.links a{display:block;color:var(--accent);word-break:break-all;margin:4px 0;font-size:14px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:10px 16px;border-radius:8px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw}
#toast.on{opacity:1}.warn{color:var(--red);font-weight:600}
.row>*{min-width:0}.row>.btn{flex:none}
button{-webkit-tap-highlight-color:transparent;touch-action:manipulation}
@media (max-width:600px){
header{position:sticky;top:0;z-index:5;padding:10px 12px;padding-top:max(10px,env(safe-area-inset-top))}
header h1{font-size:17px}nav button{padding:10px 10px}
main{padding:12px 12px max(16px,env(safe-area-inset-bottom))}.card{padding:14px;margin-bottom:12px}
.chip{padding:10px 14px;font-size:15px}
.grid{grid-template-columns:1fr 1fr}.st{min-height:64px;font-size:17px}
.moods{grid-template-columns:repeat(4,1fr)}
.btn{min-height:46px}td:first-child{width:45%}
}
</style></head><body>
<header><h1>Door Sign</h1><nav>
<button data-tab="home" class="on">Status</button><button data-tab="settings">Settings</button><button data-tab="system">System</button>
</nav></header>
<main>
<section id="home" class="on">
 <div class="card">
  <h2>On the sign now</h2>
  <img id="sign" class="sign" alt="Current sign image">
  <div class="sub" id="pvm"></div>
 </div>
 <div class="card">
  <h2>Set status</h2>
  <label>For how long</label>
  <div class="chips" id="dur">
   <button class="chip on" data-d="">Default (1 h)</button><button class="chip" data-d="15">15 min</button>
   <button class="chip" data-d="30">30 min</button><button class="chip" data-d="60">1 h</button>
   <button class="chip" data-d="120">2 h</button><button class="chip" data-d="eod">End of day</button>
   <button class="chip" data-d="clear">Until cleared</button>
  </div>
  <div class="grid" style="margin-top:14px">
   <button class="st" data-s="available">Available</button>
   <button class="st red" data-s="call">On a call</button>
   <button class="st red" data-s="meeting">In a meeting</button>
   <button class="st black" data-s="focus">Heads down</button>
   <button class="st black" data-s="busy">Busy</button>
   <button class="st" data-s="away">Away</button>
  </div>
 </div>
 <button class="btn ghost" id="clr" style="width:100%;margin-bottom:12px">Back to calendar</button>
 <div class="card">
  <h2>Custom message</h2>
  <input id="cm" maxlength="32" placeholder="Back at 3">
  <div class="row" style="margin-top:10px">
   <select id="cc"><option value="black">Black band</option><option value="red">Red band</option><option value="none">No band</option></select>
   <button class="btn" id="cgo">Show it</button>
  </div>
 </div>
 <div class="card">
  <h2>Mood</h2>
  <div class="moods" id="moods"></div>
  <button class="btn ghost" id="mclr" style="margin-top:10px">Clear mood</button>
  <div class="sub" style="text-align:left">Clears itself each morning at the start of working hours.</div>
 </div>
 <div class="card">
  <h2>Preview a time</h2>
  <p class="sub">Show the sign as it would look at another date and time, using your calendar for then. Nothing is saved, and it goes back to the real time after 30 min.</p>
  <div class="row"><input type="datetime-local" id="simat" style="flex:1"><button class="btn" id="simgo">Preview</button></div>
  <button class="btn ghost" id="simclr" style="margin-top:10px;width:100%" hidden>Back to the real time</button>
 </div>
</section>

<section id="settings">
 <div class="card">
  <h2>Sign</h2>
  <label>Name</label><input id="s_name" maxlength="32">
  <label>Title</label><input id="s_title" maxlength="40">
  <label>Humor</label><select id="s_humor"><option value="0">Off</option><option value="1">Dry</option><option value="2">Full</option></select>
  <label>Screen refresh (experimental)</label><select id="s_fast"><option value="0">Full color (about 27 s)</option><option value="1">Fast black and white: quick</option><option value="2">Fast black and white: balanced</option><option value="3">Fast black and white: strong</option></select>
  <p class="sub" style="text-align:left">Fast mode draws changes in black and white in a few seconds, then does a full color refresh after 10 quiet minutes or every 12 updates. Not supported by the panel maker.</p>
 </div>
 <div class="card">
  <h2>Working hours</h2>
  <div class="row"><div><label>Start</label><input id="s_ws" type="time"></div><div><label>End</label><input id="s_we" type="time"></div></div>
  <label>Days</label><div class="days" id="s_days"></div>
 </div>
 <div class="card">
  <h2>Calendar</h2>
  <label>Broker URL (Apps Script web app, ends in /exec)</label><input id="s_burl" placeholder="https://script.google.com/macros/s/.../exec">
  <label>Broker token (write-only)</label><input id="s_btok" type="password" autocomplete="off">
  <button class="btn ghost" id="lcal" style="margin-top:12px">Load calendars</button>
  <div id="calst" class="sub" style="text-align:left"></div>
  <div id="callist"></div>
 </div>
 <div class="card">
  <h2>PIN</h2>
  <p id="pinwarn" class="warn" hidden>Still using the default PIN. Change it.</p>
  <label>New PIN (leave blank to keep)</label><input id="s_pin" type="password" autocomplete="new-password" maxlength="12">
 </div>
 <button class="btn" id="ssave" style="width:100%">Save settings</button>
</section>

<section id="system">
 <div class="card"><h2>Device</h2><table id="sys"></table>
  <div class="row" style="margin-top:12px"><button class="btn ghost" id="rf">Redraw panel</button><button class="btn ghost" id="rb">Reboot</button></div>
  <button class="btn ghost" id="gal" style="margin-top:10px;width:100%">Show all mood faces on the panel (3 min)</button>
 </div>
 <div class="card"><h2>Firmware update</h2>
  <p>Choose a firmware.bin built with <code>pio run -d firmware</code> (it is in <code>firmware/.pio/build/esp32dev/</code>). The sign installs it and restarts; the panel redraws about a minute later.</p>
  <input type="file" id="otaf" accept=".bin">
  <button class="btn" id="otago" style="width:100%;margin-top:10px">Install update</button>
  <p id="otast"></p>
 </div>
 <div class="card"><h2>Calendar broker</h2><table id="brk"></table><pre id="brkr" hidden></pre></div>
 <div class="card links"><h2>One-tap bookmarks</h2><div id="links"><p>Enter your PIN once (set any status) to generate links.</p></div></div>
 <div class="card"><h2>Recent log</h2><pre id="log">Sign in to view.</pre></div>
</section>
</main>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let pin=null;try{pin=localStorage.getItem('dsk')}catch(e){}
let lastRenders=null;
let dur='';
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('on'),3500)}
async function login(){
 const p=prompt('Door Sign PIN');if(!p)return false;
 const r=await fetch('/login?k='+encodeURIComponent(p),{headers:{Accept:'application/json'}});
 if(!r.ok){toast('Wrong PIN');return false}
 pin=p;try{localStorage.setItem('dsk',p)}catch(e){}renderLinks();return true}
async function call(url,opt={}){
 opt.headers=Object.assign({Accept:'application/json'},opt.headers||{});
 let r=await fetch(url,opt);
 if(r.status===401){if(!await login())return null;r=await fetch(url,opt)}
 let j={};try{j=await r.json()}catch(e){}
 if(!r.ok){toast(j.error||j.message||('Error '+r.status));return null}
 return j}
async function setState(s,extra=''){
 const q=new URLSearchParams();if(dur)q.set('d',dur);
 const j=await call('/s/'+s+'?'+q.toString()+extra);
 if(j){toast(j.message+'. Sign updates in about 30 s.');setTimeout(refresh,500)}}
async function refresh(){
 try{const j=await (await fetch('/status.json')).json();
  // Reload the sign image only when the panel has actually redrawn.
  if(j.renders!==lastRenders){lastRenders=j.renders;$('#sign').src='/sign.bmp?r='+j.renders}
  $('#pvm').textContent=(j.rendering?'Updating the sign now (about 30 s). ':'')+(j.manual?'Manual override':'Automatic')+(j.stale?' (calendar data stale)':'');
  $('#clr').hidden=!j.manual;
  $$('.mood').forEach(b=>b.classList.toggle('on',b.dataset.m===j.mood));$('#mclr').hidden=!j.mood;
  if(j.mood)$('#pvm').textContent+=', mood: '+j.moodName;
  $('#simclr').hidden=!j.sim;
  if(j.sim&&j.simAt){const t=new Date(j.simAt*1000).toLocaleString([],{weekday:'short',month:'short',day:'numeric',hour:'numeric',minute:'2-digit'});
   $('#pvm').textContent='PREVIEW of '+t+' ('+Math.ceil(j.simLeft/60)+' min left). '+$('#pvm').textContent}}catch(e){}}
const MOODS=[['happy','\u{1F600}','Happy'],['chill','\u{1F60E}','Chill'],['focused','\u{1F9D0}','Focused'],['facepalm','\u{1F926}','Facepalm'],
 ['stressed','\u{1F630}','Stressed'],['grumpy','\u{1F620}','Grumpy'],['tired','\u{1F634}','Tired'],['celebrating','\u{1F973}','Celebrating'],
 ['love','\u{1F60D}','Grateful'],['meh','\u{1F610}','Meh'],['sick','\u{1F912}','Under the weather'],['mindblown','\u{1F92F}','Mind blown'],
 ['thinking','\u{1F914}','Thinking'],['hungry','\u{1F924}','Hungry'],['zen','\u{1F60C}','Zen'],['zipped','\u{1F910}','Do not disturb']];
$('#moods').innerHTML=MOODS.map(([id,e,n])=>'<button class="mood" data-m="'+id+'"><b>'+e+'</b>'+n+'</button>').join('');
async function setMood(id){const j=await call('/m/'+id);if(j){toast(j.message+'. Sign updates in about 30 s.');setTimeout(refresh,500)}}
$$('.mood').forEach(b=>b.onclick=()=>setMood(b.dataset.m));$('#mclr').onclick=()=>setMood('clear');
function renderLinks(){
 if(!pin)return;const b=location.origin,k='k='+encodeURIComponent(pin);
 const L=[['On a call 30 min','/s/call?d=30&'+k],['Meeting 1 h','/s/meeting?d=60&'+k],['Heads down 1 h','/s/focus?d=60&'+k],
  ['Busy 1 h','/s/busy?d=60&'+k],['Away until cleared','/s/away?d=clear&'+k],['Available','/s/available?'+k],['Back to calendar','/s/clear?'+k]];
 MOODS.forEach(([id,e,n])=>L.push(['Mood: '+n,'/m/'+id+'?'+k]));L.push(['Clear mood','/m/clear?'+k]);
 $('#links').innerHTML=L.map(([n,u])=>'<a href="'+u+'">'+n+': '+b+u+'</a>').join('')}
$$('nav button').forEach(b=>b.onclick=()=>{
 $$('nav button').forEach(x=>x.classList.toggle('on',x===b));
 $$('section').forEach(s=>s.classList.toggle('on',s.id===b.dataset.tab));
 if(b.dataset.tab==='settings')loadSettings();if(b.dataset.tab==='system')loadSystem()});
$$('#dur .chip').forEach(c=>c.onclick=()=>{dur=c.dataset.d;$$('#dur .chip').forEach(x=>x.classList.toggle('on',x===c))});
$$('.st').forEach(b=>b.onclick=()=>setState(b.dataset.s));
$('#cgo').onclick=()=>{const m=$('#cm').value.trim();if(!m){toast('Type a message first');return}
 setState('custom','&m='+encodeURIComponent(m)+'&c='+$('#cc').value)};
$('#clr').onclick=()=>setState('clear');
async function sim(body){const j=await call('/api/sim',{method:'POST',body});if(j){toast(j.message+'. Sign updates in about 30 s.');setTimeout(refresh,500)}}
$('#simgo').onclick=()=>{const v=$('#simat').value;if(!v){toast('Pick a date and time');return}const f=new URLSearchParams();f.set('at',v);sim(f)};
$('#simclr').onclick=()=>{const f=new URLSearchParams();f.set('clear','1');sim(f)};
const DN=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
$('#s_days').innerHTML=DN.map((d,i)=>'<label><input type="checkbox" data-i="'+i+'">'+d+'</label>').join('');
let curCals='primary:mine',calsLoaded=false;
function calMap(){const m={};curCals.split(',').forEach(p=>{p=p.trim();if(!p)return;const i=p.lastIndexOf(':');m[i>0?p.slice(0,i):p]=i>0?p.slice(i+1):'mine'});return m}
async function loadSettings(){const j=await call('/api/settings');if(!j)return;
 $('#s_name').value=j.name;$('#s_title').value=j.title;$('#s_humor').value=j.humor;$('#s_fast').value=j.fast||0;$('#s_ws').value=j.ws;$('#s_we').value=j.we;
 $$('#s_days input').forEach(c=>c.checked=!!(j.days&(1<<c.dataset.i)));$('#pinwarn').hidden=!j.defaultPin;
 $('#s_burl').value=j.brokerUrl||'';$('#s_btok').value='';$('#s_btok').placeholder=j.tokenSet?'Set (leave blank to keep)':'Not set';
 curCals=j.cals||'primary:mine';if(!calsLoaded)$('#calst').textContent='Showing: '+curCals.split(',').length+' calendar(s). Load calendars to change.'}
function renderCals(list){const m=calMap(),box=$('#callist');box.innerHTML='';
 list.forEach(c=>{const row=document.createElement('div');row.className='cal';
  const cb=document.createElement('input');cb.type='checkbox';cb.checked=c.id in m;cb.dataset.id=c.id;
  const dot=document.createElement('i');dot.className='dot';dot.style.background=c.color||'#999';
  const nm=document.createElement('span');nm.textContent=c.name+(c.primary?' (primary)':'');
  const sel=document.createElement('select');sel.innerHTML='<option value="mine">My events</option><option value="all">All events</option>';
  sel.value=m[c.id]||'mine';if(c.primary){sel.value='mine';sel.disabled=true}
  row.append(cb,dot,nm,sel);box.append(row)});
 calsLoaded=true;$('#calst').textContent='Tick the calendars that should block you, then Save.'}
$('#lcal').onclick=async()=>{$('#calst').textContent='Asking the broker...';
 if(!await call('/api/calendars?refresh=1'))return;
 for(let i=0;i<30;i++){await new Promise(r=>setTimeout(r,1000));const j=await call('/api/calendars');if(!j)return;
  if(j.state==='ready'){renderCals(j.data.calendars||[]);return}
  if(j.state==='error'){$('#calst').textContent='Failed: '+j.error+'. Save the broker URL and token first.';return}}
 $('#calst').textContent='Timed out. Check the broker URL and token on the System page.'};
$('#ssave').onclick=async()=>{const f=new URLSearchParams();
 f.set('name',$('#s_name').value);f.set('title',$('#s_title').value);f.set('humor',$('#s_humor').value);f.set('fast',$('#s_fast').value);
 f.set('ws',$('#s_ws').value);f.set('we',$('#s_we').value);
 let d=0;$$('#s_days input').forEach(c=>{if(c.checked)d|=1<<c.dataset.i});f.set('days',d);
 f.set('brokerUrl',$('#s_burl').value.trim());if($('#s_btok').value.trim())f.set('token',$('#s_btok').value.trim());
 if(calsLoaded){const parts=[];$$('#callist .cal').forEach(r=>{const cb=r.querySelector('input');if(cb.checked)parts.push(cb.dataset.id+':'+r.querySelector('select').value)});
  if(!parts.length){toast('Pick at least one calendar');return}f.set('cals',parts.join(','))}
 const np=$('#s_pin').value;if(np)f.set('pin',np);
 const j=await call('/api/settings',{method:'POST',body:f});
 if(j){if(np){pin=np;try{localStorage.setItem('dsk',np)}catch(e){}renderLinks()}$('#s_pin').value='';toast('Saved');loadSettings();setTimeout(refresh,500)}};
async function loadSystem(){const j=await (await fetch('/api/system',{headers:{Accept:'application/json'}})).json();
 const up=j.uptime,h=Math.floor(up/3600),m=Math.floor(up%3600/60);
 const rows=[['Firmware',j.fw],['IP address',j.ip],['Hostname',j.host],['MAC',j.mac],['Network',j.ssid+' ('+j.rssi+' dBm)'],
  ['Uptime',h+' h '+m+' min'],['Clock',j.timeValid?j.time:'not synced'],['Free heap',j.heap+' (min '+j.heapMin+')'],
  ['Last refresh',(j.renderMs/1000).toFixed(1)+' s'+(j.renderFast?' (fast)':'')+(j.rendering?' (drawing now)':'')],['Refreshes',j.renders]];
 $('#sys').innerHTML=rows.map(r=>'<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>').join('');
 const b=j.broker||{},ago=s=>s<0?'never':s<90?s+' s ago':Math.round(s/60)+' min ago';
 const br=b.configured?[['Last poll',ago(b.lastPollAgo)],['Last success',ago(b.lastOkAgo)],['Error',b.error||'none']]:[['Status','Not configured. Add the broker URL and token in Settings.']];
 const bt=$('#brk');bt.innerHTML='';br.forEach(r=>{const tr=bt.insertRow();tr.insertCell().textContent=r[0];tr.insertCell().textContent=r[1]});
 $('#brkr').hidden=!b.response;$('#brkr').textContent=b.response||'';
 if(j.log!==undefined)$('#log').textContent=j.log;renderLinks()}
$('#rf').onclick=async()=>{if(await call('/api/refresh',{method:'POST'}))toast('Redrawing, about 30 s')};
$('#gal').onclick=async()=>{if(await call('/api/gallery',{method:'POST'}))toast('Drawing all 16 faces, about 30 s')};
$('#rb').onclick=async()=>{if(confirm('Reboot the sign?')&&await call('/api/reboot',{method:'POST'}))toast('Rebooting')};
$('#otago').onclick=async()=>{const f=$('#otaf').files[0];if(!f){toast('Choose a firmware.bin first');return}
 if(!confirm('Install '+f.name+' ('+Math.round(f.size/1024)+' KB) and restart the sign?'))return;
 const fd=new FormData();fd.append('firmware',f,f.name);$('#otago').disabled=true;$('#otast').textContent='Uploading...';
 const j=await call('/api/ota',{method:'POST',body:fd});$('#otago').disabled=false;
 if(!j){$('#otast').textContent='Update failed. The sign keeps running the old firmware.';return}
 $('#otast').textContent=j.message+' Reconnecting...';
 const t0=Date.now(),poll=async()=>{try{const s=await (await fetch('/api/system',{headers:{Accept:'application/json'}})).json();
  if(s.uptime<120){$('#otast').textContent='Back online, firmware '+s.fw+'.';loadSystem();return}}catch(e){}
  if(Date.now()-t0<120000)setTimeout(poll,3000);else $('#otast').textContent='Not back after 2 min. Check the panel or power-cycle it.'};setTimeout(poll,5000)};
refresh();setInterval(refresh,5000);renderLinks();
</script></body></html>)HTML";
