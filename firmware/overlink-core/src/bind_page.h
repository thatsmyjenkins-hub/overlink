#pragma once

// Second-screen companion — phone keyboard for Hubspace / HA / Hue / Wink / ratgdo
static const char BIND_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="theme-color" content="#0A1210"/>
<title>OVERLINK BIND</title>
<style>
:root{--bg:#0A1210;--panel:#122018;--cyan:#3DDC97;--amber:#F0A030;--dim:#7A8F80;--active:#E8F5A0;--font:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--cyan);font-family:var(--font)}
body{padding:0 0 env(safe-area-inset-bottom,0)}
header{display:flex;justify-content:space-between;align-items:center;padding:.75rem .85rem;border-bottom:1px solid var(--cyan)}
header h1{margin:0;font-size:.9rem;letter-spacing:.1em}
header a{color:var(--amber);text-decoration:none;font-size:.75rem}
main{padding:.8rem .85rem 1.4rem;max-width:26rem;margin:0 auto}
h2{margin:.2rem 0 .45rem;font-size:.8rem;color:var(--amber);letter-spacing:.08em}
.muted{color:var(--dim);font-size:.78rem;line-height:1.4}
.item{border:1px solid var(--cyan);border-radius:3px;padding:.7rem;margin-top:.45rem;background:var(--panel)}
.tile{appearance:none;width:100%;text-align:left;background:var(--panel);color:var(--cyan);border:1px solid var(--cyan);border-radius:3px;padding:.75rem .7rem;font:inherit;margin-top:.4rem;cursor:pointer}
.tile strong{display:block;letter-spacing:.06em}
.tile .muted{margin-top:.2rem}
.tile:disabled{opacity:.45;cursor:default}
input,button.chip{font:inherit}
input{width:100%;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.7rem .65rem;margin-top:.45rem;border-radius:3px;font-size:1rem}
button.chip{appearance:none;background:var(--panel);color:var(--cyan);border:1px solid var(--cyan);border-radius:3px;padding:.7rem .6rem;letter-spacing:.06em;cursor:pointer;width:100%;margin-top:.55rem;text-transform:uppercase}
button.chip.on{background:var(--active);color:var(--bg)}
button.chip.danger{border-color:var(--amber);color:var(--amber)}
.code{font-size:2rem;letter-spacing:.28em;color:var(--active);text-align:center;padding:.6rem 0}
.toast{position:fixed;left:.7rem;right:.7rem;bottom:1rem;background:var(--panel);border:1px solid var(--cyan);color:var(--active);padding:.65rem .8rem;display:none;z-index:5}
.toast.show{display:block}
.ok{color:var(--active)} .bad{color:var(--amber)}
</style>
</head>
<body>
<header><h1>OVERLINK BIND</h1><a href="/">CTRL</a></header>
<main id="app"></main>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const toast=m=>{const t=$('#toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2400)};
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function api(path,opts){const r=await fetch(path,opts);const t=await r.text();let j;try{j=JSON.parse(t)}catch{j={raw:t}} if(!r.ok) throw new Error(j.error||j.message||t); return j}
async function post(path,body){return api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})})}
const TYPES=[
  {id:'hubspace',name:'HUBSPACE',blurb:'Front lock, entry lamp, power strip. Email + password (OTP if Hubspace asks).',fields:['email','password'],import:true},
  {id:'homeassistant',name:'HOME ASSISTANT',blurb:'Local bridge. Paste a long-lived token from HA profile.',fields:['baseUrl','token'],import:true},
  {id:'hue',name:'PHILIPS HUE',blurb:'Press the bridge link button, then tap LINK on this phone.',fields:['hueIp'],import:true},
  {id:'wink',name:'WINK',blurb:'Cloud login if you still have a subscription / token. Local hub needs that token.',fields:['email','password','token','clientId'],import:true},
  {id:'garage',name:'GARAGE / RATGDO',blurb:'Chamberlain MyQ cloud is locked. Enter a ratgdo / ESPHome IP.',fields:['ratgdoIp'],import:false},
  {id:'nest',name:'NEST / GOOGLE',blurb:'OAuth device flow not wired yet.',disabled:true},
  {id:'ring',name:'RING',blurb:'OAuth device flow not wired yet.',disabled:true}
];
const q=new URLSearchParams(location.search);
let sess=q.get('s')||'';
let type=q.get('type')||'';
let info={status:'idle'};
let busy=false;
function fieldHtml(id){
  if(id==='email') return `<input id="email" type="email" autocomplete="username" placeholder="email" autocapitalize="off"/>`;
  if(id==='password') return `<input id="password" type="password" autocomplete="current-password" placeholder="password"/>`;
  if(id==='token') return `<input id="token" autocomplete="off" placeholder="access / long-lived token (optional)"/>`;
  if(id==='baseUrl') return `<input id="baseUrl" inputmode="url" placeholder="http://192.168.x.x:8123" autocapitalize="off"/>`;
  if(id==='clientId') return `<input id="clientId" placeholder="Wink client_id (optional)" autocapitalize="off"/>`;
  if(id==='hueIp') return `<input id="hueIp" inputmode="decimal" placeholder="Hue bridge IP (blank = discover)" autocapitalize="off"/>`;
  if(id==='ratgdoIp') return `<input id="ratgdoIp" inputmode="decimal" placeholder="ratgdo IP e.g. 192.168.4.50" autocapitalize="off"/>`;
  return '';
}
function render(){
  const t=TYPES.find(x=>x.id===type);
  let html='';
  if(info.status==='ok'){
    html=`<h2 class="ok">LINKED</h2><div class="item"><div class="muted">${esc(info.message||'bound')}</div></div>
      <a class="chip" href="/" style="display:block;text-align:center;text-decoration:none;margin-top:.7rem;border:1px solid var(--cyan);padding:.7rem">OPEN CTRL</a>`;
    $('#app').innerHTML=html; return;
  }
  if(info.status==='fail'){
    html=`<h2 class="bad">BIND FAILED</h2><div class="item"><div class="muted">${esc(info.message||'failed')}</div></div>
      <button class="chip on" id="retry">TRY AGAIN</button>
      <a class="chip" href="/" style="display:block;text-align:center;text-decoration:none;margin-top:.55rem;border:1px solid var(--cyan);padding:.7rem">OPEN CTRL</a>`;
    $('#app').innerHTML=html;
    $('#retry')&&($('#retry').onclick=()=>{info.status='wait';render()});
    return;
  }
  if(!t){
    html=`<div class="muted">Phone keyboard for cloud logins. TV / Ops → CONN can show a code + QR — or start here.</div><h2>BIND</h2>`;
    if(info.status==='wait'&&info.type){
      html+=`<div class="item"><div class="muted">Core is waiting</div><div class="code">${esc(info.code||'••••••')}</div>
        <div class="muted">${esc((info.type||'').toUpperCase())} · open this phone on house Wi‑Fi</div></div>
        <button class="chip on" id="resume">CONTINUE ${esc((info.type||'').toUpperCase())}</button>`;
    }
    html+=TYPES.map(x=>`<button class="tile" data-type="${x.id}" ${x.disabled?'disabled':''}><strong>${esc(x.name)}</strong><div class="muted">${esc(x.blurb)}</div></button>`).join('');
    $('#app').innerHTML=html;
    $('#resume')&&($('#resume').onclick=()=>{type=info.type;render()});
    document.querySelectorAll('[data-type]').forEach(b=>b.onclick=()=>{if(b.disabled)return;type=b.dataset.type;render()});
    return;
  }
  html=`<button class="chip" id="back" style="width:auto">BACK</button>
    <h2>${esc(t.name)}</h2><div class="muted">${esc(t.blurb)}</div>`;
  if(info.status==='wait'&&info.code) html+=`<div class="code">${esc(info.code)}</div>`;
  html+=t.fields.map(fieldHtml).join('');
  if(t.import) html+=`<label class="muted" style="display:block;margin-top:.55rem"><input type="checkbox" id="doImport" checked/> import devices after link</label>`;
  html+=`<button class="chip on" id="go">${t.id==='hue'?'LINK HUE':(t.id==='garage'?'SAVE IP':'LINK + IMPORT')}</button>
    <div class="muted" id="msg" style="margin-top:.5rem">${esc(info.message||'')}</div>`;
  $('#app').innerHTML=html;
  $('#back').onclick=()=>{type='';render()};
  $('#go').onclick=submit;
}
async function submit(){
  if(busy) return; busy=true;
  const t=TYPES.find(x=>x.id===type);
  const body={type, session:sess||info.session||'', import:!!$('#doImport')?.checked};
  (t.fields||[]).forEach(f=>{const el=$('#'+f); if(el) body[f]=el.value.trim()});
  if(body.password===undefined && $('#password')) body.password=$('#password').value;
  $('#msg').textContent='linking…';
  try{
    const r=await post('/api/bind/complete',body);
    info={...info,...r,status:r.ok?'ok':'fail',message:r.message||''};
    toast(r.message||(r.ok?'linked':'fail'));
    render();
  }catch(e){
    info={...info,status:'fail',message:e.message||'bind fail'};
    toast(e.message);
    render();
  } finally { busy=false }
}
async function boot(){
  try{
    const path=sess?('/api/bind/status?s='+encodeURIComponent(sess)):'/api/bind/status';
    info=await api(path);
    if(!type && info.status==='wait' && info.type) type=info.type;
  }catch{info={status:'idle'}}
  render();
}
boot();
</script>
</body>
</html>
)HTML";
