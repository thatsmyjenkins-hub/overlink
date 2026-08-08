#pragma once

// Overlink phone UI — Home · Zones · Scenes · Ops (CTRL chrome)
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="theme-color" content="#0A1210"/>
<title>OVERLINK</title>
<style>
:root{
  --bg:#0A1210;--panel:#122018;--cyan:#3DDC97;--amber:#F0A030;
  --dim:#7A8F80;--active:#E8F5A0;--text:#E8F5A0;
  --font:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;background:var(--bg);color:var(--cyan);font-family:var(--font)}
body{padding:0 0 3.5rem}
header{display:flex;justify-content:space-between;align-items:center;padding:.7rem .8rem;border-bottom:1px solid var(--cyan)}
header h1{margin:0;font-size:.95rem;letter-spacing:.08em;font-weight:600;color:var(--cyan)}
#wifi{color:var(--active);font-size:.8rem}
main{padding:.65rem .7rem;max-width:28rem;margin:0 auto}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.35rem}
button.tile{
  appearance:none;background:var(--panel);color:var(--cyan);
  border:1px solid var(--cyan);border-radius:3px;padding:.85rem .4rem;
  font:inherit;letter-spacing:.06em;text-transform:uppercase;cursor:pointer
}
button.tile.accent{border-color:var(--amber);color:var(--amber)}
button.tile.active{background:var(--active);color:var(--bg);border-color:var(--cyan)}
button.tile.danger{border-color:var(--amber);color:var(--amber)}
.now{margin-top:.65rem;border:1px solid var(--cyan);border-radius:3px;background:var(--panel);padding:.55rem}
.now-top{display:flex;justify-content:space-between;margin-bottom:.45rem}
.now-top strong{color:var(--active);font-size:.85rem}
.now-top span{color:var(--amber);font-size:.8rem}
.row{display:flex;flex-wrap:wrap;gap:.3rem;margin-top:.35rem}
button.chip{
  appearance:none;background:var(--panel);color:var(--cyan);
  border:1px solid var(--cyan);border-radius:3px;padding:.55rem .45rem;
  font:inherit;font-size:.75rem;letter-spacing:.04em;cursor:pointer;flex:1;min-width:3.2rem
}
button.chip:hover{box-shadow:0 0 10px rgba(61,220,151,.25)}
button.chip.on{background:var(--active);color:var(--bg)}
.vol{color:var(--active);min-width:2rem;text-align:center;align-self:center}
.feed{margin-top:.5rem;color:var(--cyan);font-size:.75rem;min-height:1.2rem}
.item{display:flex;justify-content:space-between;align-items:center;border:1px solid var(--cyan);
  border-radius:3px;padding:.55rem .6rem;margin-top:.35rem;background:var(--panel)}
.muted{color:var(--dim);font-size:.75rem}
.kv{display:grid;grid-template-columns:5rem 1fr;gap:.25rem .5rem;font-size:.8rem}
.kv .muted{font-size:.75rem}
.stat{border:1px solid var(--cyan);border-radius:3px;padding:.55rem;background:var(--panel);margin-top:.35rem}
.stat strong{color:var(--active);display:block;font-size:1.1rem}
nav{position:fixed;left:0;right:0;bottom:0;display:flex;border-top:1px solid var(--cyan);background:#07100e}
nav button{flex:1;appearance:none;background:transparent;border:0;color:var(--dim);padding:.85rem .2rem;font:inherit;font-size:.65rem;letter-spacing:.06em;cursor:pointer}
nav button.active{color:var(--cyan)}
.toast{position:fixed;left:.7rem;right:.7rem;bottom:3.6rem;background:var(--panel);border:1px solid var(--cyan);
  color:var(--active);padding:.65rem .8rem;display:none;z-index:5}
.toast.show{display:block}
.hidden{display:none!important}
h2{margin:.4rem 0;font-size:.85rem;color:var(--amber);letter-spacing:.08em}
</style>
</head>
<body>
<header>
  <h1 id="hdr">OVERLINK</h1>
  <div id="wifi">WIFI --</div>
</header>
<main id="app"></main>
<nav id="nav">
  <button data-tab="home" class="active">HOME</button>
  <button data-tab="zones">ZONES</button>
  <button data-tab="scenes">SCENES</button>
  <button data-tab="games">GAMES</button>
  <button data-tab="ops">OPS</button>
</nav>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const ZONE_SCENES=[
  {id:'full',tag:'FULL'},{id:'chill',tag:'CHILL'},
  {id:'movie',tag:'MOVIE'},{id:'game',tag:'GAME'},
  {id:'sports',tag:'SPORTS'},{id:'bed',tag:'BED'},
  {id:'dance',tag:'DANCE',accent:1},{id:'date',tag:'DATE',accent:1},
  {id:'karaoke',tag:'KARAOKE',accent:1},{id:'off',tag:'OFF',confirm:1}
];
const loadTab=()=>{try{return localStorage.getItem('ol_tab')||'home'}catch{return'home'}};
const saveTab=t=>{try{localStorage.setItem('ol_tab',t)}catch{}};
const loadZone=()=>{try{return localStorage.getItem('ol_zone')||''}catch{return''}};
const saveZone=z=>{try{localStorage.setItem('ol_zone',z)}catch{}};

const WLED_FX=[
  {id:0,n:'Solid'},{id:1,n:'Blink'},{id:2,n:'Breathe'},{id:9,n:'Rainbow'},
  {id:38,n:'Aurora'},{id:59,n:'Comet'},{id:88,n:'Candle'},{id:115,n:'Fire'}
];
const WLED_COLORS=[
  {n:'WARM',r:255,g:180,b:90},{n:'COOL',r:180,g:210,b:255},{n:'RED',r:255,g:40,b:40},
  {n:'GREEN',r:40,g:220,b:80},{n:'BLUE',r:40,g:100,b:255},{n:'PURPLE',r:180,g:40,b:255}
];
let state={
  wifi:{},devices:[],zones:[],tab:loadTab(),zone:loadZone(),active:null,
  feed:'> GRID READY',vol:18,forceSetup:false,summary:null,
  homeScenes:[],automations:[],grids:[],grideye:null,discover:null,opsPanel:'lab',
  wled:null, wledId:'',
  arrival:false, arrivalStep:'scan', connectors:[], relay:null,
  party:null, partySweep:null, printers:[]
};
const toast=m=>{const t=$('#toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2200)};
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function api(path,opts){const r=await fetch(path,opts);const t=await r.text();let j;try{j=JSON.parse(t)}catch{j={raw:t}} if(!r.ok) throw new Error(j.error||j.message||t); return j}
async function post(path,body){return api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})})}

async function runScene(id,confirm){
  if(confirm && !window.confirm('Engage OFF?')) return;
  try{
    const r=await post('/api/scenes/run',{id});
    state.active=id; state.feed='> ENGAGE '+id.toUpperCase();
    toast(r.message||id); await refreshSummary(); render();
  }catch(e){toast(e.message)}
}
async function avFetch(path,body){
  const ctrl=new AbortController();
  const t=setTimeout(()=>ctrl.abort(),20000);
  try{
    const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{}),signal:ctrl.signal});
    const text=await r.text();
    let j; try{j=JSON.parse(text)}catch{j={raw:text}}
    if(!r.ok) throw new Error(j.message||j.error||text||('HTTP '+r.status));
    return j;
  } finally { clearTimeout(t) }
}
async function avVol(delta){
  toast(delta>=0?'VOL +':'VOL −');
  try{const r=await avFetch('/api/av/vol',{delta});if(typeof r.level==='number')state.vol=r.level;state.feed='> '+(r.message||'vol');toast(r.message||'vol');renderZoneOnly()}catch(e){toast(e.name==='AbortError'?'vol timeout':e.message)}
}
async function avApp(id){
  toast('… '+String(id).toUpperCase());
  try{const r=await avFetch('/api/av/app',{id});state.feed='> '+(r.message||id.toUpperCase());toast(r.message||id);renderZoneOnly()}catch(e){toast(e.name==='AbortError'?'app timeout':e.message)}
}
async function avKey(name){
  toast(name);
  try{const r=await avFetch('/api/av/key',{name});state.feed='> '+(r.message||name);toast(r.message||name);renderZoneOnly()}catch(e){toast(e.name==='AbortError'?'key timeout':e.message)}
}
async function avWatch(){toast('WATCH…');try{const r=await avFetch('/api/av/watch',{});state.feed='> WATCH';toast(r.message||'watch');renderZoneOnly()}catch(e){toast(e.name==='AbortError'?'watch timeout':e.message)}}
async function avInput(target){toast('IN '+target);try{const r=await avFetch('/api/av/input',{target});state.feed='> IN '+target;toast(r.message||target);renderZoneOnly()}catch(e){toast(e.name==='AbortError'?'input timeout':e.message)}}
async function setDev(id,on,bri){
  try{
    const body={id,on}; if(bri!=null) body.bri=bri;
    await post('/api/devices/set',body);
    await refreshDevices();
    toast(on?(bri!=null?('BRI '+bri):'ON'):'OFF');
    render();
  }catch(e){toast(e.message)}
}
async function identify(id){try{const r=await post('/api/devices/identify',{id});toast(r.message||'blink');state.feed='> ID '+id}catch(e){toast(e.message)}}
async function refreshWled(){
  try{
    const q=state.wledId?('?id='+encodeURIComponent(state.wledId)):'';
    state.wled=await api('/api/wled/state'+q);
    if(state.wled&&state.wled.id) state.wledId=state.wled.id;
  }catch(e){
    state.wled={ok:false,online:false,name:'WLED',ip:'',message:String(e.message||e)};
  }
}
async function wledSet(patch){
  try{
    const body=Object.assign({id:state.wledId||undefined},patch);
    const r=await post('/api/wled/set',body);
    if(r.state&&r.state.ok) state.wled=r.state;
    else await refreshWled();
    state.feed='> WLED '+(r.message||'ok');
    toast(r.message||'wled');
    renderZoneOnlyWled();
  }catch(e){toast(e.message)}
}
function renderZoneOnlyWled(){
  if(!(state.tab==='zones'&&state.zone==='basement')) return;
  const box=$('#wledBox'); if(!box){render(); return}
  box.outerHTML=renderWledPanel();
  bindWled();
  const f=document.querySelector('.feed'); if(f) f.textContent=state.feed;
}
function renderWledPanel(){
  const w=state.wled;
  if(!w || (!w.id && w.ok!==false)){
    return `<div class="now" id="wledBox" style="margin-top:.45rem">
      <div class="now-top"><strong>WLED</strong><span>…</span></div>
      <div class="muted" style="padding:.35rem 0">Loading strip…</div>
      <div class="row"><button class="chip" id="wledRefresh">REFRESH</button></div></div>`;
  }
  if(w.ok===false || w.online===false){
    return `<div class="now" id="wledBox" style="margin-top:.45rem">
      <div class="now-top"><strong>WLED</strong><span style="color:var(--amber)">DARK</span></div>
      <div class="muted">${esc(w.name||'strip')} · ${esc(w.ip||'')}</div>
      <div class="row"><button class="chip" id="wledRefresh">RETRY</button></div></div>`;
  }
  const bri=w.bri|0, on=!!w.on;
  const fxName=(WLED_FX.find(x=>x.id===(w.fx|0))||{n:'FX '+ (w.fx|0)}).n;
  return `<div class="now" id="wledBox" style="margin-top:.45rem">
    <div class="now-top"><strong>WLED · ${esc(w.name||'STRIP')}</strong>
      <span style="color:${on?'var(--active)':'var(--amber)'}">${on?'ON':'OFF'} · ${bri}</span></div>
    <div class="row">
      <button class="chip ${on?'on':''}" data-wled-on="1">ON</button>
      <button class="chip ${!on?'on':''}" data-wled-on="0">OFF</button>
      <button class="chip" data-wled-bri="-32">BRI −</button>
      <button class="chip" data-wled-bri="32">BRI +</button>
      <button class="chip" id="wledRefresh">↻</button>
    </div>
    <div class="muted" style="margin:.25rem 0">color</div>
    <div class="row">${WLED_COLORS.map(c=>`<button class="chip" data-wled-rgb="${c.r},${c.g},${c.b}">${c.n}</button>`).join('')}</div>
    <div class="muted" style="margin:.25rem 0">effect · ${esc(fxName)} · spd ${w.sx|0} · int ${w.ix|0}</div>
    <div class="row">${WLED_FX.map(f=>`<button class="chip ${(w.fx|0)===f.id?'on':''}" data-wled-fx="${f.id}">${f.n}</button>`).join('')}</div>
    <div class="row">
      <button class="chip" data-wled-sx="-20">SPD −</button><button class="chip" data-wled-sx="20">SPD +</button>
      <button class="chip" data-wled-ix="-20">INT −</button><button class="chip" data-wled-ix="20">INT +</button>
    </div>
    <div class="muted" style="margin:.25rem 0">presets</div>
    <div class="row">${[1,2,3,4,5,6,7,8].map(p=>`<button class="chip" data-wled-ps="${p}">P${p}</button>`).join('')}</div>
  </div>`;
}
function bindWled(){
  const wr=$('#wledRefresh'); if(wr) wr.onclick=async()=>{await refreshWled();render()};
  document.querySelectorAll('[data-wled-on]').forEach(b=>b.onclick=()=>wledSet({on:b.dataset.wledOn==='1'}));
  document.querySelectorAll('[data-wled-bri]').forEach(b=>b.onclick=()=>{
    const cur=(state.wled&&state.wled.bri)|128;
    wledSet({on:true,bri:Math.max(1,Math.min(255,cur+(+b.dataset.wledBri)))});
  });
  document.querySelectorAll('[data-wled-rgb]').forEach(b=>b.onclick=()=>{
    const [r,g,bl]=b.dataset.wledRgb.split(',').map(Number);
    wledSet({on:true,solid:true,r,g,b:bl});
  });
  document.querySelectorAll('[data-wled-fx]').forEach(b=>b.onclick=()=>wledSet({on:true,fx:+b.dataset.wledFx}));
  document.querySelectorAll('[data-wled-sx]').forEach(b=>b.onclick=()=>{
    const cur=(state.wled&&state.wled.sx)|128;
    wledSet({sx:Math.max(0,Math.min(255,cur+(+b.dataset.wledSx)))});
  });
  document.querySelectorAll('[data-wled-ix]').forEach(b=>b.onclick=()=>{
    const cur=(state.wled&&state.wled.ix)|128;
    wledSet({ix:Math.max(0,Math.min(255,cur+(+b.dataset.wledIx)))});
  });
  document.querySelectorAll('[data-wled-ps]').forEach(b=>b.onclick=()=>wledSet({on:true,ps:+b.dataset.wledPs}));
}

function renderDash(){
  const s=state.summary||{};
  const sum=s.summary||{};
  const online=sum.deviceOnline??'—', total=sum.deviceTotal??'—';
  const scene=sum.lastSceneTag||'—';
  const hasDeck=(state.devices||[]).some(d=>d.type==='cyberdeck');
  const deck=sum.deckOnline?'UP':'DARK';
  return `<h2>HOME</h2>
  <div class="stat"><span class="muted">devices</span><strong>${online}/${total} ONLINE</strong></div>
  <div class="stat"><span class="muted">recent scene</span><strong>${esc(scene)}</strong></div>
  ${hasDeck?`<div class="stat"><span class="muted">cyberdeck</span><strong>${deck}</strong></div>`:''}
  <div class="stat"><span class="muted">link</span><strong>${esc(state.wifi.staSsid||'—')}</strong>
    <div class="muted">${esc(state.wifi.staIp||'')}</div></div>
  <div class="row" style="margin-top:.6rem;flex-wrap:wrap">
    ${(state.zones||[]).slice(0,6).map(z=>`<button class="chip" data-goto-zone="${esc(z.id)}">${esc((z.name||z.id).toUpperCase())} ></button>`).join('')||`<button class="chip" data-goto-zone="basement">BASEMENT CTRL ></button>`}
  </div>
  <div class="feed">${esc(state.feed)}</div>`;
}
function zoneDevices(zid){return (state.devices||[]).filter(d=>d.zoneId===zid)}
function zoneMeta(zid){
  const z=(state.zones||[]).find(x=>x.id===zid);
  return {id:zid, name:(z&&z.name)||zid};
}
function renderZones(){
  const rows=(state.zones||[]).map(z=>{
    const devs=zoneDevices(z.id);
    const lights=devs.filter(d=>d.type==='hue'||d.type==='hue_group'||d.type==='wiz_bulb'||d.type==='wled');
    const amazon=devs.filter(d=>d.type==='firetv'||d.type==='cast');
    const up=devs.filter(d=>d.online).length;
    const blurb=z.id==='basement'?'CTRL · scenes · AV · WLED'
      :[lights.length?`${lights.length} lights`:null, amazon.length?amazon.map(a=>a.type).join('+'):null].filter(Boolean).join(' · ')||'room';
    return `<div class="item" data-goto-zone="${esc(z.id)}" style="cursor:pointer"><div>
      <strong style="color:var(--cyan)">${esc((z.name||z.id).toUpperCase())}</strong>
      <div class="muted">${esc(blurb)} · ${up}/${devs.length||0} up</div></div><span class="muted">></span></div>`;
  }).join('')||`<div class="muted">No zones yet — Ops → Hue Sync</div>`;
  return `<h2>ZONES</h2>
  <div class="muted" style="margin-bottom:.4rem">Hue rooms + Basement AV. Tap a room for CTRL.</div>
  ${rows}
  <div class="row" style="margin-top:.5rem"><button class="chip" id="syncRooms">SYNC HUE ROOMS</button></div>
  <div class="feed">${esc(state.feed)}</div>`;
}
function renderRoomCtrl(){
  const z=zoneMeta(state.zone);
  const devs=zoneDevices(state.zone);
  const groups=devs.filter(d=>d.type==='hue_group');
  const bulbs=devs.filter(d=>d.type==='hue');
  const amazon=devs.filter(d=>d.type==='firetv'||d.type==='cast');
  const wiz=devs.filter(d=>d.type==='wiz_bulb'||d.type==='wled');
  const primary=groups[0];
  const title=(z.name||z.id).toUpperCase()+' CTRL';
  const groupBtns=primary?`<div class="row">
      <button class="chip on" data-id="${esc(primary.id)}" data-on="1">ROOM ON</button>
      <button class="chip" data-id="${esc(primary.id)}" data-on="0">ROOM OFF</button>
      <button class="chip" data-id="${esc(primary.id)}" data-on="1" data-bri="80">DIM</button>
      <button class="chip" data-id="${esc(primary.id)}" data-on="1" data-bri="200">BRIGHT</button>
    </div>
    <div class="muted" style="margin:.35rem 0">${esc(primary.name)}${groups.length>1?' · +'+(groups.length-1)+' groups':''}</div>`
    :`<div class="muted">No Hue room group in this zone</div>`;
  const extraGroups=groups.slice(1).map(g=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(g.name)}</strong>
    <div class="muted">hue group · ${g.online?'up':'dark'}</div></div>
    <div class="row" style="margin:0;flex:0;gap:.25rem">
      <button class="chip" data-id="${esc(g.id)}" data-on="1">ON</button>
      <button class="chip" data-id="${esc(g.id)}" data-on="0">OFF</button>
    </div></div>`).join('');
  const lightRows=bulbs.map(d=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(d.name)}</strong>
    <div class="muted">hue · ${d.online?'up':'dark'}</div></div>
    <div class="row" style="margin:0;flex:0;gap:.25rem">
      <button class="chip" data-id="${esc(d.id)}" data-on="1">ON</button>
      <button class="chip" data-id="${esc(d.id)}" data-on="0">OFF</button>
    </div></div>`).join('')||'<div class="muted">No individual Hue lights listed</div>';
  const otherLights=wiz.map(d=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(d.name)}</strong>
    <div class="muted">${esc(d.type)} · ${d.online?'up':'dark'}</div></div>
    <div class="row" style="margin:0;flex:0;gap:.25rem">
      <button class="chip" data-id="${esc(d.id)}" data-on="1">ON</button>
      <button class="chip" data-id="${esc(d.id)}" data-on="0">OFF</button>
    </div></div>`).join('');
  let av='';
  const fire=amazon.find(d=>d.type==='firetv');
  const cast=amazon.filter(d=>d.type==='cast');
  if(fire||cast.length){
    av=`<div class="now" style="margin-top:.45rem"><div class="now-top"><strong>AMAZON / CAST</strong><span>${fire&&fire.online?'FIRE UP':(cast.some(c=>c.online)?'CAST UP':'DARK')}</span></div>`;
    if(fire){
      av+=`<div class="muted" style="margin-bottom:.3rem">${esc(fire.name)} · Fire TV apps</div>
      <div class="row">
        <button class="chip" data-app="nflx">NFLX</button><button class="chip" data-app="yt">YT</button>
        <button class="chip" data-app="disney">DSN+</button><button class="chip" data-app="prime">PRIME</button>
      </div>
      <div class="row">
        <button class="chip" data-key="HOME">HOME</button><button class="chip" data-key="BACK">BACK</button>
        <button class="chip" data-key="OK">OK</button><button class="chip" data-watch="1">WATCH</button>
      </div>`;
    }
    if(cast.length){
      av+=cast.map(c=>`<div class="muted" style="margin-top:.35rem">${esc(c.name)} · Chromecast · ${c.online?'online':'dark'} · cast from phone apps</div>`).join('');
    }
    av+=`</div>`;
  }
  return `<div class="row" style="margin:0 0 .4rem"><button class="chip" data-tabjump="zones" data-clear-zone="1" style="flex:0">BACK</button>
    <strong style="align-self:center;color:var(--active)">${esc(title)}</strong></div>
  <div class="now">
    <div class="now-top"><strong>HUE ROOM</strong><span>${devs.filter(d=>d.online).length}/${devs.length} UP</span></div>
    ${groupBtns}
  </div>
  ${extraGroups}
  <h2 style="margin-top:.6rem">LIGHTS</h2>
  ${lightRows}${otherLights}
  ${av}
  <div class="feed">${esc(state.feed)}</div>`;
}
function renderBasementCtrl(){
  return `<div class="row" style="margin:0 0 .4rem"><button class="chip" data-tabjump="zones" data-clear-zone="1" style="flex:0">BACK</button>
    <strong style="align-self:center;color:var(--active)">BASEMENT CTRL</strong></div>
  <div class="grid">${ZONE_SCENES.map(s=>`<button class="tile ${s.accent?'accent':''} ${s.confirm?'danger':''} ${state.active===s.id?'active':''}" data-sc="${s.id}" ${s.confirm?'data-confirm=1':''}>${s.tag}</button>`).join('')}</div>
  <div class="now">
    <div class="now-top"><strong>MAIN | READY</strong><span>READY</span></div>
    <div class="row">
      <button class="chip" data-key="HOME">MAIN</button>
      <button class="chip" data-vol="-1">-</button>
      <span class="vol" id="vol">${String(state.vol).padStart(2,'0')}</span>
      <button class="chip" data-vol="1">+</button>
      <button class="chip" data-watch="1">WATCH</button>
      <button class="chip" data-input="fire">IN</button>
    </div>
    <div class="row">
      <button class="chip" data-app="nflx">NFLX</button><button class="chip" data-app="yt">YT</button>
      <button class="chip" data-app="disney">DSN+</button><button class="chip" data-app="prime">PRIME</button>
    </div>
    <div class="row">
      <button class="chip" data-key="HOME">HOME</button><button class="chip" data-key="BACK">BACK</button>
      <button class="chip" data-key="OK">OK</button><button class="chip" data-key="MORE">MORE</button>
    </div>
    <div class="row">
      <button class="chip" data-tabjump="ops" data-ops="lab">DEVICES ></button>
      <button class="chip" data-app="ps5">PS5</button>
    </div>
    <div class="feed">${esc(state.feed)}</div>
  </div>
  ${renderWledPanel()}`;
}
function renderHomeScenes(){
  const list=state.homeScenes.length?state.homeScenes.map(s=>`<button class="tile ${s.confirm?'danger':''}" data-sc="${s.id}" ${s.confirm?'data-confirm=1':''}>${esc(s.tag||s.name||s.id)}</button>`).join(''):'';
  return `<h2>HOME SCENES</h2>
  ${list?`<div class="grid">${list}</div>`:`<div class="muted">No home-wide scenes yet.</div>
  <div class="row"><button class="chip" data-goto-zone="basement">OPEN BASEMENT CTRL</button></div>`}
  <div class="feed">${esc(state.feed)}</div>`;
}
function renderGames(){
  return `<h2>GAMES</h2>
  <div class="item"><div>
    <strong style="color:var(--cyan)">Grace's Party Pack</strong>
    <div class="muted">14 party games · phone = controller · TV/WallDeck = big screen</div>
  </div></div>
  <div class="row" style="margin-top:.5rem">
    <a class="chip" style="text-align:center;text-decoration:none;display:block" href="/games/grace/">PLAY (phone)</a>
    <a class="chip" style="text-align:center;text-decoration:none;display:block" href="/games/grace/?present=1" target="_blank">TV PAGE</a>
  </div>
  <div class="item" style="margin-top:.6rem"><div class="muted">
    <b style="color:var(--amber)">Cast the TV PAGE only</b> (not Play). iOS: Control Center → Screen Mirroring. Android: Cast → Cast tab.<br/>
    Play = phone controller (secret cards stay here). TV/WallDeck = room view (timer, scores; hides Don't Say It / Charades words).<br/>
    WallDeck: Home → GRACE PARTY (no cast needed).
  </div></div>
  <div class="feed">${esc(state.feed)}</div>`;
}
function renderArrival(){
  const step=state.arrivalStep||'scan';
  const sug=(state.discover&&state.discover.suggestions)||[];
  const cats=state.discover&&state.discover.catalog||['lights','av','cameras','locks'];
  const hueBridge=sug.find(s=>s.type==='hue_bridge');
  if(step==='rooms'){
    const z=(state.zones||[]).map(z=>`<div class="item"><strong style="color:var(--cyan)">${esc(z.name)}</strong><div class="muted">${esc(z.id)}</div></div>`).join('')||'<div class="muted">no rooms yet</div>';
    const assign=(state.devices||[]).map(d=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(d.name)}</strong>
      <div class="muted">${esc(d.type)}</div></div>
      <select data-assign="${esc(d.id)}" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.35rem;font:inherit">
        ${(state.zones||[]).map(z=>`<option value="${esc(z.id)}" ${d.zoneId===z.id?'selected':''}>${esc(z.name||z.id)}</option>`).join('')}
      </select></div>`).join('')||'<div class="muted">add devices first</div>';
    return `<div class="muted">ARRIVAL · ROOMS</div>
      <h2>ASSIGN ROOMS</h2>
      <div class="row"><input id="newZoneId" placeholder="id (kitchen)" style="flex:1;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="newZoneName" placeholder="name" style="flex:1;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <button class="chip" id="addZone">ADD ROOM</button></div>
      <h2>ROOMS</h2>${z}<h2>DEVICES</h2>${assign}
      <div class="row" style="margin-top:.6rem">
        <button class="chip" id="arrivalBack">BACK</button>
        <button class="chip on" id="arrivalFinish">FINISH</button>
      </div>
      <div class="feed">${esc(state.feed)}</div>`;
  }
  const ranked=cats.map(cat=>{
    const rows=sug.filter(s=>(s.catalog||'')===cat||(!s.catalog&&cat==='other'));
    if(!rows.length) return '';
    return `<h2>${esc(cat.toUpperCase())}</h2>`+rows.map(s=>`<div class="item"><div>
      <strong style="color:var(--cyan)">${esc(s.name)}</strong>
      <div class="muted">${esc(s.type)} · ${esc(s.fallbackIp||'')} · ${esc(s.reason||'')}</div></div>
      <button class="chip" data-add-id="${esc(s.id)}">ADD</button></div>`).join('');
  }).join('')||'<div class="muted">scanning… or no peers yet</div>';
  return `<div class="muted">ARRIVAL · NEW HOME</div>
    <h2>FOUND ${sug.length} DEVICE${sug.length===1?'':'S'}</h2>
    <div class="row">
      <button class="chip" id="arrivalScan">SCAN LAN</button>
      <button class="chip" id="arrivalAddAll">ADD ALL KNOWN</button>
      <button class="chip" id="arrivalSkip">SKIP TO HOME</button>
    </div>
    ${hueBridge?`<div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem;margin-top:.5rem">
      <strong style="color:var(--amber)">HUE BRIDGE @ ${esc(hueBridge.fallbackIp)}</strong>
      <div class="muted">Press the link button on the bridge, then Link Hue.</div>
      <button class="chip" id="huePair">LINK HUE</button>
    </div>`:''}
    ${ranked}
    <div class="row" style="margin-top:.6rem">
      <button class="chip" id="arrivalRooms">ASSIGN ROOMS ></button>
      <button class="chip on" id="arrivalFinish">DONE</button>
    </div>
    <div class="feed">${esc(state.feed)}</div>`;
}
function renderOps(){
  const p=state.opsPanel||'lab';
  const tabs=`<div class="row">
    <button class="chip ${p==='party'?'on':''}" data-ops="party">PARTY</button>
    <button class="chip ${p==='lab'?'on':''}" data-ops="lab">LAB</button>
    <button class="chip ${p==='add'?'on':''}" data-ops="add">ADD</button>
    <button class="chip ${p==='cam'?'on':''}" data-ops="cam">CAM</button>
    <button class="chip ${p==='conn'?'on':''}" data-ops="conn">CONN</button>
    <button class="chip ${p==='expose'?'on':''}" data-ops="expose">EXPOSE</button>
    <button class="chip ${p==='nets'?'on':''}" data-ops="nets">NETS</button>
    <button class="chip ${p==='auto'?'on':''}" data-ops="auto">AUTO</button>
    <button class="chip ${p==='eye'?'on':''}" data-ops="eye">EYE</button>
    <button class="chip ${p==='about'?'on':''}" data-ops="about">ABOUT</button>
  </div>`;
  if(p==='party'){
    const ble=(state.party&&state.party.ble)||{};
    const sw=state.partySweep||{};
    const mdns=(sw.mdns||[]).slice(0,24).map(m=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(m.name||m.service)}</strong>
      <div class="muted">${esc(m.kind)} · ${esc(m.ip)}:${m.port||''}</div></div></div>`).join('')||'<div class="muted">run Sweep</div>';
    const bleRows=(sw.ble||[]).slice(0,24).map(b=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(b.name)}</strong>
      <div class="muted">${esc(b.addr)} · ${b.rssi} dBm</div></div></div>`).join('')||'<div class="muted">run Sweep</div>';
    const printers=(state.printers||[]).map(pr=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(pr.name)}</strong>
      <div class="muted">${esc(pr.ip)}:${pr.port} · ${esc(pr.via)}${pr.openGuess?' · likely open':''}</div></div>
      <button class="chip" data-print-ip="${esc(pr.ip)}" data-print-port="${pr.port}">PRINT</button></div>`).join('')||'<div class="muted">find printers first</div>';
    const bleLineText=esc((ble.lines&&ble.lines.length?ble.lines:[
      'OVERLINK IS IN THE HOUSE','YOUR FRIDGE IS JUDGING YOU','DAD MODE ENABLED','UNLOCK THE SNACKS'
    ]).join('\n'));
    return `${tabs}<h2>PARTY TRICKS</h2>
      <div class="muted">Family fun + a little LAN awareness. Only on networks you own / with household OK.</div>
      <h2>1 · SWEEP</h2>
      <div class="muted">mDNS roll call + BLE names nearby (what announces itself).</div>
      <div class="row"><button class="chip" id="partySweep">SWEEP LAN + BLE</button></div>
      <div class="muted">mdns ${sw.mdnsCount??'—'} · ble ${sw.bleCount??'—'}</div>
      <h2>mDNS</h2>${mdns}<h2>BLE SEEN</h2>${bleRows}
      <h2>2 · BLE BILLBOARD</h2>
      <div class="muted">Phones see this name when they open Bluetooth settings. Cycles = spam funny lines. Does not force pairing popups (Flipper Continuity floods) — scan-list awareness only.</div>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <input id="bleMsg" placeholder="main message" value="${esc(ble.message||'OVERLINK SAYS HI')}" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <textarea id="bleLines" rows="5" placeholder="one funny line per row" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit;white-space:pre">${bleLineText}</textarea>
        <div class="row">
          <button class="chip" id="bleStart">BROADCAST</button>
          <button class="chip" id="bleCycle">SPAM CYCLE</button>
          <button class="chip" id="bleStop">STOP</button>
        </div>
        <div class="muted">${ble.advertising?('LIVE: '+esc(ble.current||'')):'billboard off'}</div>
      </div>
      <h2>3 · STAMPEDE</h2>
      <div class="muted">Blink every Hue / WiZ / WLED — lights confess themselves.</div>
      <div class="row"><button class="chip" id="partyStampede">STAMPEDE</button></div>
      <h2>4 · CAST STINGER</h2>
      <div class="muted">Big message on a Chromecast (needs Cast online + TV on).</div>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <input id="castMsg" placeholder="TV message" value="OVERLINK ONLINE" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="castIp" placeholder="cast ip (optional)" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <button class="chip" id="partyCast">CAST STINGER</button>
      </div>
      <h2>5 · PRINTER</h2>
      <div class="muted">Find open IPP / JetDirect printers and print your line. Lesson: open printers accept jobs from anyone on the LAN.</div>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <input id="printMsg" placeholder="message to print" value="Hello from Overlink — lock down guest printing!" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <div class="row">
          <button class="chip" id="findPrinters">FIND PRINTERS</button>
        </div>
      </div>
      ${printers}
      <div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='lab'){
    const rows=state.devices.map(d=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(d.name)}</strong>
      <div class="muted">${esc(d.type)} · ${d.online?'uplink':'dark'} · ${esc(d.ip||'')}</div></div>
      <div class="row" style="margin:0;flex:0;gap:.25rem">
        ${(d.type==='wiz_bulb'||d.type==='wled'||d.type==='hue'||d.type==='hue_group'||d.type==='ha_entity')?`<button class="chip" data-id="${esc(d.id)}" data-on="1">ON</button>
        <button class="chip" data-id="${esc(d.id)}" data-on="0">OFF</button>
        <button class="chip" data-identify="${esc(d.id)}">ID</button>`:`<span class="muted">${d.online?'UP':'DARK'}</span>`}
      </div></div>`).join('')||'<div class="muted">no devices</div>';
    return `${tabs}<h2>DEVICE LAB</h2>
      <div class="row"><button class="chip" id="probe">PROBE</button><button class="chip" id="smoke">SMOKE</button>
        <button class="chip" id="rerunArrival">ARRIVAL</button></div>
      ${rows}<div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='add'){
    const sug=((state.discover&&state.discover.suggestions)||[]).map(s=>`<div class="item"><div>
      <strong style="color:var(--cyan)">${esc(s.name)}</strong>
      <div class="muted">${esc(s.type)} · ${esc(s.fallbackIp||'')} · ${esc(s.reason||'')}</div></div>
      <button class="chip" data-add-id="${esc(s.id)}">ADD</button></div>`).join('')||'<div class="muted">no suggestions — scan or sync Hue</div>';
    return `${tabs}<h2>ADD DEVICES</h2>
      <div class="row">
        <button class="chip" id="scanPeers">SCAN PEERS</button>
        <button class="chip" id="hueSync">HUE SYNC</button>
        <button class="chip" id="huePair">LINK HUE</button>
      </div>
      <h2>SUGGESTIONS</h2>${sug}
      <h2>MANUAL</h2>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <input id="addId" placeholder="id (cast-kitchen)" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="addName" placeholder="name" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <select id="addType" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit">
          <option value="hue">hue</option><option value="hue_group">hue_group</option>
          <option value="wiz_bulb">wiz_bulb</option><option value="wled">wled</option>
          <option value="cyberdeck">cyberdeck</option><option value="vizio">vizio</option>
          <option value="firetv">firetv</option><option value="sony">sony</option>
          <option value="tcl">tcl</option><option value="cast">cast</option>
          <option value="ps5">ps5</option><option value="camera">camera</option>
          <option value="ha_entity">ha_entity</option>
        </select>
        <input id="addIp" placeholder="ip" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="addHue" placeholder="hueId (optional)" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <button class="chip" id="addManual">SAVE DEVICE</button>
      </div>
      <div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='cam'){
    const cams=(state.devices||[]).filter(d=>d.type==='camera');
    const tiles=cams.map(d=>`<div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
      <strong style="color:var(--cyan)">${esc(d.name)}</strong>
      <div class="muted">${esc(d.ip||'')} · ${d.online?'up':'dark'}</div>
      <img alt="snap" id="snap-${esc(d.id)}" style="width:100%;max-height:180px;object-fit:contain;background:#020806;border:1px solid var(--cyan)" src="/api/cameras/snapshot?id=${encodeURIComponent(d.id)}&t=${Date.now()}"/>
      <button class="chip" data-resnap="${esc(d.id)}">REFRESH</button>
    </div>`).join('')||'<div class="muted">no cameras — scan LAN (RTSP :554) or add type camera</div>';
    return `${tabs}<h2>CAMERAS</h2>
      <div class="row"><button class="chip" id="scanPeers">SCAN PEERS</button></div>
      ${tiles}<div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='conn'){
    const rows=(state.connectors||[]).map(c=>`<div class="item"><div>
      <strong style="color:var(--cyan)">${esc(c.name||c.id)}</strong>
      <div class="muted">${esc(c.type)} · ${esc(c.transport)} · ${c.hasSecret?'token':'no secret'}</div></div>
      <button class="chip" data-conn-del="${esc(c.id)}">DEL</button></div>`).join('')||'<div class="muted">no connectors</div>';
    return `${tabs}<h2>CONNECTORS</h2>
      <div class="muted">Cloud / bridge logins stored per-grid on SD.</div>
      ${rows}
      <h2>ADD</h2>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <select id="connType" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit">
          <option value="homeassistant">Home Assistant (bridge)</option>
          <option value="nest">Nest / Google Home</option>
          <option value="ring">Ring</option>
        </select>
        <input id="connBase" placeholder="base URL (HA: http://ip:8123)" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="connToken" placeholder="token / refresh token" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <div class="row">
          <button class="chip" id="connSave">SAVE</button>
          <button class="chip" id="connHaImport">HA IMPORT</button>
        </div>
      </div>
      <div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='expose'){
    const r=state.relay||{};
    return `${tabs}<h2>EXPOSE GRID</h2>
      <div class="muted">Optional Overlink relay — Core dials out; no inbound ports.</div>
      <div class="item"><div class="kv">
        <div class="muted">status</div><div>${r.enabled?(r.connected?'LIVE':'WAITING'):'OFF'}</div>
        <div class="muted">grid</div><div>${esc(r.grid||'')}</div>
        <div class="muted">error</div><div>${esc(r.error||'—')}</div>
      </div></div>
      <div class="item" style="flex-direction:column;align-items:stretch;gap:.35rem">
        <input id="relayUrl" placeholder="http://relay-host:8787" value="${esc(r.url||'')}" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <input id="relayCode" placeholder="enroll code" style="background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.45rem;font:inherit"/>
        <div class="row">
          <button class="chip" id="relaySaveUrl">SAVE URL</button>
          <button class="chip" id="relayEnroll">ENROLL</button>
          <button class="chip ${r.enabled?'on':''}" id="relayToggle">${r.enabled?'DISABLE':'EXPOSE'}</button>
        </div>
      </div>
      <div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='nets'){
    const grids=(state.grids||[]).map(g=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(g.name)}${g.active?' ★':''}</strong>
      <div class="muted">${esc(g.ssid||'unbound')} · ${esc(g.id)}</div></div>
      ${g.active?'<span class="muted">ACTIVE</span>':`<button class="chip" data-grid="${esc(g.id)}">SWITCH</button>`}
      </div>`).join('')||'<div class="muted">no grids</div>';
    return `${tabs}<h2>NETWORKS / GRIDS</h2>
      <div class="item"><div class="kv">
        <div class="muted">ssid</div><div>${esc(state.wifi.staSsid||'—')}</div>
        <div class="muted">ip</div><div>${esc(state.wifi.staIp||'')}</div>
        <div class="muted">saved</div><div>${(state.wifi.saved||[]).map(esc).join(', ')||'—'}</div>
      </div></div>
      <div class="row" style="margin-top:.5rem">
        <button class="chip" id="setupWifi">WIFI SETUP</button>
        <button class="chip" id="discover">DISCOVER</button>
      </div>
      <h2>GRIDS</h2>${grids}
      <div class="row"><input id="newGrid" placeholder="new grid name" style="flex:2;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.5rem;font:inherit"/>
        <button class="chip" id="createGrid">CREATE</button></div>
      <div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='auto'){
    const rows=(state.automations||[]).map(a=>`<div class="item"><div>
      <strong style="color:var(--cyan)">${esc(a.name)}</strong>
      <div class="muted">${String(a.hour).padStart(2,'0')}:${String(a.minute).padStart(2,'0')} → ${esc(a.actionId)}</div></div>
      <div class="row" style="margin:0;flex:0">
        <button class="chip ${a.enabled?'on':''}" data-auto="${esc(a.id)}" data-en="${a.enabled?0:1}">${a.enabled?'ON':'OFF'}</button>
        <button class="chip" data-autorun="${esc(a.id)}">RUN</button>
      </div></div>`).join('')||'<div class="muted">no automations</div>';
    return `${tabs}<h2>AUTOMATIONS</h2>${rows}<div class="feed">${esc(state.feed)}</div>`;
  }
  if(p==='eye'){
    const g=state.grideye||{};
    const aps=(g.aps||[]).map(a=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(a.ssid||'?')}</strong>
      <div class="muted">${a.rssi} dBm · ${esc(a.enc)}</div></div></div>`).join('')||'<div class="muted">no AP scan yet</div>';
    const hosts=(g.hosts||[]).map(h=>`<div class="item"><div><strong style="color:var(--cyan)">${esc(h.name)}</strong>
      <div class="muted">${esc(h.ip)} · ${h.online?'up':'dark'}</div></div></div>`).join('');
    return `${tabs}<h2>GRIDEYE</h2>
      <div class="stat"><span class="muted">phase</span><strong>${esc(g.phaseLabel||g.phase||'IDLE')}</strong>
        <div class="muted">APs ${g.wifiCount??0} · hosts ${g.hostCount??0}</div></div>
      <div class="row"><button class="chip" id="eyeScan">RF SCAN</button></div>
      <h2>APS</h2>${aps}<h2>HOSTS</h2>${hosts}
      <div class="feed">${esc(state.feed)}</div>`;
  }
  return `${tabs}<h2>ABOUT</h2>
    <div class="item"><div class="kv">
      <div class="muted">product</div><div>Overlink Core</div>
      <div class="muted">grid</div><div>Home Sprawl</div>
      <div class="muted">ui</div><div>CTRL IA</div>
    </div></div>
    <div class="feed">${esc(state.feed)}</div>`;
}
function renderSetup(){
  return `<div class="muted">SoftAP — pick house Wi‑Fi</div>
  <div class="row"><button class="chip" id="rescan">RESCAN</button></div>
  <div id="list" class="muted" style="margin-top:.5rem">scanning…</div>
  <div id="joinCard" class="hidden" style="margin-top:.6rem;border:1px solid var(--cyan);padding:.6rem">
    <strong id="joinSsid" style="color:var(--active)"></strong>
    <input id="pass" type="password" placeholder="passphrase" style="width:100%;margin:.4rem 0;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.5rem;font:inherit"/>
    <input id="gridName" placeholder="grid name (optional)" style="width:100%;margin:.4rem 0;background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.5rem;font:inherit"/>
    <button class="chip" id="joinBtn">JOIN + REBOOT</button>
  </div>`;
}
async function loadScan(){
  try{
    const items=(await api('/api/wifi/scan')).networks||[];
    const list=$('#list'); if(!list) return;
    list.innerHTML=items.map((n,i)=>`<div class="item"><div>${esc(n.ssid)}<div class="muted">${n.rssi} dBm</div></div>
      <button class="chip" data-i="${i}">SELECT</button></div>`).join('')||'no beacons';
    list.querySelectorAll('button').forEach(b=>b.onclick=()=>{
      const n=items[+b.dataset.i];
      $('#joinCard').classList.remove('hidden');
      $('#joinSsid').textContent=n.ssid;
      $('#joinBtn').onclick=async()=>{
        try{
          const gn=$('#gridName')?.value?.trim();
          if(gn){
            try{await post('/api/grids/create',{name:gn,ssid:n.ssid})}
            catch{await post('/api/grids/rename',{name:gn}).catch(()=>{})}
          }
          await post('/api/wifi/save',{ssid:n.ssid,pass:$('#pass').value});
          toast('rebooting…');
        }catch(e){toast(e.message)}
      };
    });
  }catch(e){const list=$('#list'); if(list) list.textContent='scan failed'}
}
function bind(){
  document.querySelectorAll('[data-sc]').forEach(b=>b.onclick=()=>runScene(b.dataset.sc,!!b.dataset.confirm));
  document.querySelectorAll('[data-vol]').forEach(b=>b.onclick=()=>avVol(+b.dataset.vol));
  document.querySelectorAll('[data-app]').forEach(b=>b.onclick=()=>avApp(b.dataset.app));
  document.querySelectorAll('[data-key]').forEach(b=>b.onclick=()=>avKey(b.dataset.key));
  document.querySelectorAll('[data-watch]').forEach(b=>b.onclick=()=>avWatch());
  document.querySelectorAll('[data-input]').forEach(b=>b.onclick=()=>avInput(b.dataset.input));
  document.querySelectorAll('[data-on]').forEach(b=>b.onclick=()=>setDev(b.dataset.id,b.dataset.on==='1', b.dataset.bri!=null?+b.dataset.bri:undefined));
  const syncRooms=$('#syncRooms'); if(syncRooms) syncRooms.onclick=async()=>{
    try{const r=await post('/api/hue/sync',{});toast(r.message||'synced');await refreshZones();await refreshDevices();state.feed='> HUE ROOMS';render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-identify]').forEach(b=>b.onclick=()=>identify(b.dataset.identify));
  document.querySelectorAll('[data-goto-zone]').forEach(b=>b.onclick=()=>{
    state.zone=b.dataset.gotoZone;saveZone(state.zone);state.tab='zones';saveTab('zones');
    if(state.zone==='basement') state.wled=null;
    render();
  });
  document.querySelectorAll('[data-tabjump]').forEach(b=>b.onclick=()=>{
    state.tab=b.dataset.tabjump;
    if(b.dataset.ops) state.opsPanel=b.dataset.ops;
    if(b.dataset.clearZone){ state.zone=''; saveZone(''); }
    saveTab(state.tab); render();
  });
  document.querySelectorAll('[data-ops]').forEach(b=>b.onclick=()=>{state.opsPanel=b.dataset.ops;render()});
  document.querySelectorAll('[data-auto]').forEach(b=>b.onclick=async()=>{
    try{await post('/api/automations/enable',{id:b.dataset.auto,enabled:b.dataset.en==='1'});await refreshAutos();toast('saved');render()}catch(e){toast(e.message)}
  });
  document.querySelectorAll('[data-autorun]').forEach(b=>b.onclick=async()=>{
    try{const r=await post('/api/automations/run',{id:b.dataset.autorun});toast(r.message||'ran');state.feed='> AUTO '+b.dataset.autorun}catch(e){toast(e.message)}
  });
  const probe=$('#probe'); if(probe) probe.onclick=async()=>{await post('/api/devices/probe');await refreshDevices();toast('probe done');render()};
  const smoke=$('#smoke'); if(smoke) smoke.onclick=async()=>{try{const r=await post('/api/lab/smoke',{});toast(r.ok?'smoke pass':'smoke fail');state.feed='> SMOKE'}catch(e){toast(e.message)}};
  const sw=$('#setupWifi'); if(sw) sw.onclick=()=>{state.forceSetup=true;render()};
  const rs=$('#rescan'); if(rs) rs.onclick=loadScan;
  const disc=$('#discover'); if(disc) disc.onclick=async()=>{try{const d=await api('/api/discover');toast(`known ${d.known?.length||0} · nets ${d.networks?.length||0}`);state.feed='> DISCOVER'}catch(e){toast(e.message)}};
  const scanPeers=$('#scanPeers'); if(scanPeers) scanPeers.onclick=async()=>{
    try{state.discover=await api('/api/discover/devices');toast(`${(state.discover.suggestions||[]).length} suggestions`);state.feed='> SCAN PEERS';render()}catch(e){toast(e.message)}
  };
  const hueSync=$('#hueSync'); if(hueSync) hueSync.onclick=async()=>{
    try{const r=await post('/api/hue/sync',{});toast(r.message||'synced');await refreshDevices();state.discover=await api('/api/discover/devices');state.feed='> HUE SYNC';render()}catch(e){toast(e.message)}
  };
  const huePair=$('#huePair'); if(huePair) huePair.onclick=async()=>{
    try{
      const bridge=(state.discover?.suggestions||[]).find(s=>s.type==='hue_bridge');
      const r=await post('/api/hue/pair',{ip:bridge?.fallbackIp||''});
      toast(r.message||'linked');
      try{await post('/api/hue/sync',{})}catch{}
      await refreshDevices();
      state.discover=await api('/api/discover/devices');
      state.feed='> HUE LINK';
      render();
    }catch(e){toast(e.message)}
  };
  async function addSuggestion(s){
    if(!s||s.type==='hue_bridge') return;
    const body={id:s.id,type:s.type,name:s.name,zoneId:s.zoneId||'main',fallbackIp:s.fallbackIp||'',port:s.port||80};
    if(s.hueId) body.hueId=s.hueId;
    if(s.hostname) body.hostname=s.hostname;
    if(s.mac) body.mac=s.mac;
    if(s.snapshotPath) body.snapshotPath=s.snapshotPath;
    return post('/api/devices/add',body);
  }
  document.querySelectorAll('[data-add-id]').forEach(b=>b.onclick=async()=>{
    try{
      const s=(state.discover?.suggestions||[]).find(x=>x.id===b.dataset.addId);
      if(!s) return toast('missing suggestion');
      if(s.type==='hue_bridge') return toast('use Link Hue');
      const r=await addSuggestion(s);
      toast(r.message||'added'); await refreshDevices(); state.discover=await api('/api/discover/devices'); render();
    }catch(e){toast(e.message)}
  });
  const arrivalScan=$('#arrivalScan'); if(arrivalScan) arrivalScan.onclick=async()=>{
    try{toast('scanning…');state.discover=await api('/api/discover/devices');state.feed='> SCAN '+((state.discover.suggestions||[]).length);render()}catch(e){toast(e.message)}
  };
  const arrivalAddAll=$('#arrivalAddAll'); if(arrivalAddAll) arrivalAddAll.onclick=async()=>{
    try{
      const list=(state.discover?.suggestions||[]).filter(s=>s.type!=='hue_bridge');
      let n=0;
      for(const s of list){try{await addSuggestion(s);n++}catch{}}
      await refreshDevices();
      state.discover=await api('/api/discover/devices');
      toast('added '+n); state.feed='> ADD '+n; render();
    }catch(e){toast(e.message)}
  };
  const finishArrival=async()=>{
    try{await post('/api/arrival/done',{done:true});state.arrival=false;state.arrivalStep='scan';state.feed='> HOME READY';toast('arrival done');render()}catch(e){toast(e.message)}
  };
  const arrivalSkip=$('#arrivalSkip'); if(arrivalSkip) arrivalSkip.onclick=finishArrival;
  const arrivalFinish=$('#arrivalFinish'); if(arrivalFinish) arrivalFinish.onclick=finishArrival;
  const arrivalRooms=$('#arrivalRooms'); if(arrivalRooms) arrivalRooms.onclick=()=>{state.arrivalStep='rooms';render()};
  const arrivalBack=$('#arrivalBack'); if(arrivalBack) arrivalBack.onclick=()=>{state.arrivalStep='scan';render()};
  const addZone=$('#addZone'); if(addZone) addZone.onclick=async()=>{
    const id=$('#newZoneId')?.value?.trim(), name=$('#newZoneName')?.value?.trim()||id;
    if(!id) return toast('room id?');
    try{await post('/api/zones/upsert',{id,name});await refreshZones();toast('room '+id);render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-assign]').forEach(sel=>{
    sel.onchange=async()=>{
      try{await post('/api/devices/update',{id:sel.dataset.assign,zoneId:sel.value});await refreshDevices();toast('assigned')}catch(e){toast(e.message)}
    };
  });
  const rerunArrival=$('#rerunArrival'); if(rerunArrival) rerunArrival.onclick=async()=>{
    try{await post('/api/arrival/done',{done:false});state.arrival=true;state.arrivalStep='scan';toast('arrival…');state.discover=await api('/api/discover/devices');render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-resnap]').forEach(b=>b.onclick=()=>{
    const img=document.getElementById('snap-'+b.dataset.resnap);
    if(img) img.src='/api/cameras/snapshot?id='+encodeURIComponent(b.dataset.resnap)+'&t='+Date.now();
  });
  const connSave=$('#connSave'); if(connSave) connSave.onclick=async()=>{
    const type=$('#connType')?.value||'homeassistant';
    const id=type;
    const baseUrl=$('#connBase')?.value?.trim()||'';
    const token=$('#connToken')?.value?.trim()||'';
    try{
      await post('/api/connectors/upsert',{id,name:type,type,transport:type==='homeassistant'?'bridge':'cloud',baseUrl,enabled:true});
      if(token) await post('/api/connectors/secret',{id,secret:{token}});
      await refreshConnectors(); toast('connector saved'); state.feed='> CONN '+id; render();
    }catch(e){toast(e.message)}
  };
  const connHaImport=$('#connHaImport'); if(connHaImport) connHaImport.onclick=async()=>{
    try{const r=await post('/api/connectors/ha/import',{});toast(r.message||'import');await refreshDevices();render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-conn-del]').forEach(b=>b.onclick=async()=>{
    try{await post('/api/connectors/remove',{id:b.dataset.connDel});await refreshConnectors();render()}catch(e){toast(e.message)}
  });
  const relaySaveUrl=$('#relaySaveUrl'); if(relaySaveUrl) relaySaveUrl.onclick=async()=>{
    try{await post('/api/relay/configure',{url:$('#relayUrl')?.value?.trim()||''});await refreshRelay();toast('url saved');render()}catch(e){toast(e.message)}
  };
  const relayEnroll=$('#relayEnroll'); if(relayEnroll) relayEnroll.onclick=async()=>{
    try{
      const url=$('#relayUrl')?.value?.trim();
      if(url) await post('/api/relay/configure',{url});
      const r=await post('/api/relay/enroll',{code:$('#relayCode')?.value?.trim()||''});
      toast(r.message||'enrolled'); await refreshRelay(); render();
    }catch(e){toast(e.message)}
  };
  const relayToggle=$('#relayToggle'); if(relayToggle) relayToggle.onclick=async()=>{
    try{const on=!(state.relay&&state.relay.enabled);await post('/api/relay/expose',{enabled:on});await refreshRelay();toast(on?'exposed':'hidden');render()}catch(e){toast(e.message)}
  };
  const addManual=$('#addManual'); if(addManual) addManual.onclick=async()=>{
    const id=$('#addId')?.value?.trim(), name=$('#addName')?.value?.trim(), type=$('#addType')?.value;
    const fallbackIp=$('#addIp')?.value?.trim(), hueId=$('#addHue')?.value?.trim();
    if(!id||!name||!type) return toast('id/name/type?');
    try{
      const body={id,name,type,zoneId:'main'};
      if(fallbackIp) body.fallbackIp=fallbackIp;
      if(hueId) body.hueId=hueId;
      if(type==='cast') body.port=8008;
      if(type==='camera') body.port=554;
      if(type==='cyberdeck') body.hostname='cyberdeck';
      const r=await post('/api/devices/add',body);
      toast(r.message||'added'); await refreshDevices(); state.feed='> ADD '+id; render();
    }catch(e){toast(e.message)}
  };
  const cg=$('#createGrid'); if(cg) cg.onclick=async()=>{
    const name=$('#newGrid')?.value?.trim(); if(!name) return toast('name?');
    try{const r=await post('/api/grids/create',{name,ssid:state.wifi.staSsid||''});toast(r.message||'created');await refreshGrids();render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-grid]').forEach(b=>b.onclick=async()=>{
    try{
      const r=await post('/api/grids/activate',{id:b.dataset.grid});
      toast(r.message||'switched');
      state.feed='> GRID '+b.dataset.grid;
      await refreshGrids();
      render();
    }catch(e){toast(e.message)}
  });
  const eye=$('#eyeScan'); if(eye) eye.onclick=async()=>{try{const r=await post('/api/grideye/scan',{});toast(r.message||'scan');await refreshEye();render()}catch(e){toast(e.message)}};
  const partySweep=$('#partySweep'); if(partySweep) partySweep.onclick=async()=>{
    try{toast('sweeping…');state.partySweep=await post('/api/party/sweep',{});toast(state.partySweep.message||'sweep');state.feed='> SWEEP';render()}catch(e){toast(e.message)}
  };
  const blePayload=()=>{
    const message=$('#bleMsg')?.value?.trim()||'OVERLINK SAYS HI';
    const lines=($('#bleLines')?.value||'').split(/\r?\n/).map(s=>s.trim()).filter(Boolean).slice(0,8);
    return {message,lines};
  };
  const bleStart=$('#bleStart'); if(bleStart) bleStart.onclick=async()=>{
    try{const r=await post('/api/party/ble/start',Object.assign(blePayload(),{cycle:false}));toast(r.message||'ble');await refreshParty();state.feed='> BLE ON';render()}catch(e){toast(e.message)}
  };
  const bleCycle=$('#bleCycle'); if(bleCycle) bleCycle.onclick=async()=>{
    try{const r=await post('/api/party/ble/start',Object.assign(blePayload(),{cycle:true}));toast(r.message||'cycle');await refreshParty();state.feed='> BLE SPAM';render()}catch(e){toast(e.message)}
  };
  const bleStop=$('#bleStop'); if(bleStop) bleStop.onclick=async()=>{
    try{const r=await post('/api/party/ble/stop',{});toast(r.message||'stop');await refreshParty();render()}catch(e){toast(e.message)}
  };
  const partyStampede=$('#partyStampede'); if(partyStampede) partyStampede.onclick=async()=>{
    try{toast('stampede…');const r=await post('/api/party/stampede',{});toast(r.message||'done');state.feed='> STAMPEDE'}catch(e){toast(e.message)}
  };
  const partyCast=$('#partyCast'); if(partyCast) partyCast.onclick=async()=>{
    try{
      const body={message:$('#castMsg')?.value?.trim()||'OVERLINK ONLINE'};
      const ip=$('#castIp')?.value?.trim(); if(ip) body.ip=ip;
      toast('casting…');const r=await post('/api/party/cast',body);toast(r.message||'cast');state.feed='> CAST';
    }catch(e){toast(e.message)}
  };
  const findPrinters=$('#findPrinters'); if(findPrinters) findPrinters.onclick=async()=>{
    try{toast('finding printers…');const r=await api('/api/party/printers');state.printers=r.printers||[];toast(r.message||('printers '+(state.printers.length)));state.feed='> PRINTERS';render()}catch(e){toast(e.message)}
  };
  document.querySelectorAll('[data-print-ip]').forEach(b=>b.onclick=async()=>{
    try{
      const message=$('#printMsg')?.value?.trim()||'Hello from Overlink';
      const r=await post('/api/party/print',{ip:b.dataset.printIp,port:+b.dataset.printPort||9100,message});
      toast(r.message||'printed');state.feed='> PRINT';
    }catch(e){toast(e.message)}
  });
  bindWled();
}
function renderZoneOnly(){ if(state.tab==='zones'&&state.zone) render(); else { const f=document.querySelector('.feed'); if(f) f.textContent=state.feed; const v=$('#vol'); if(v) v.textContent=String(state.vol).padStart(2,'0'); } }
function render(){
  const setup=!state.wifi.sta||state.forceSetup;
  const arrival=!!state.arrival&&!setup;
  $('#nav').classList.toggle('hidden',setup||arrival);
  $('#wifi').textContent=state.wifi.sta?'WIFI OK':'WIFI --';
  $('#wifi').style.color=state.wifi.sta?'var(--active)':'var(--amber)';
  const zname=state.zone?zoneMeta(state.zone).name:'';
  $('#hdr').textContent=arrival?'ARRIVAL':(state.tab==='zones'&&state.zone?(state.zone==='basement'?'BASEMENT CTRL':(zname.toUpperCase()+' CTRL')):'OVERLINK');
  const app=$('#app');
  if(setup){app.innerHTML=renderSetup();bind();loadScan();return}
  if(arrival){app.innerHTML=renderArrival();bind();return}
  let html='';
  if(state.tab==='home') html=renderDash();
  else if(state.tab==='zones'){
    if(!state.zone) html=renderZones();
    else if(state.zone==='basement') html=renderBasementCtrl();
    else html=renderRoomCtrl();
  }
  else if(state.tab==='scenes') html=renderHomeScenes();
  else if(state.tab==='games') html=renderGames();
  else html=renderOps();
  app.innerHTML=html;
  bind();
  if(state.tab==='zones'&&state.zone==='basement'&&state.wled===null){
    state.wled={}; // loading sentinel — avoid re-entry loop
    refreshWled().then(()=>render());
  }
  document.querySelectorAll('#nav button').forEach(b=>{
    b.classList.toggle('active',b.dataset.tab===state.tab);
    b.onclick=()=>{state.tab=b.dataset.tab; if(b.dataset.tab!=='zones') state.zone=''; saveTab(state.tab); saveZone(state.zone); render();
      if(state.tab==='ops') refreshOpsData();
      if(state.tab==='home') refreshSummary();
      if(state.tab==='scenes') refreshScenes();
    };
  });
}
async function refreshDevices(){try{state.devices=(await api('/api/devices')).devices||[]}catch{state.devices=[]}}
async function refreshZones(){
  try{
    const j=await api('/api/zones');
    state.zones=(j.zones||[]).slice().sort((a,b)=>(a.sort|0)-(b.sort|0));
  }catch{state.zones=[{id:'main',name:'Main',sort:0}]}
}
async function refreshSummary(){try{state.summary=await api('/api/home/summary')}catch{state.summary=null}}
async function refreshScenes(){
  try{
    const all=(await api('/api/scenes')).scenes||[];
    state.homeScenes=all.filter(s=>(s.scope||'')==='home');
  }catch{state.homeScenes=[]}
}
async function refreshAutos(){try{state.automations=(await api('/api/automations')).automations||[]}catch{state.automations=[]}}
async function refreshGrids(){try{state.grids=(await api('/api/grids')).grids||[]}catch{state.grids=[]}}
async function refreshEye(){try{state.grideye=(await api('/api/grideye/summary')).summary||null}catch{state.grideye=null}}
async function refreshConnectors(){try{state.connectors=(await api('/api/connectors')).connectors||[]}catch{state.connectors=[]}}
async function refreshRelay(){try{state.relay=(await api('/api/relay/status')).relay||null}catch{state.relay=null}}
async function refreshParty(){try{state.party=await api('/api/party/status')}catch{state.party=null}}
async function refreshOpsData(){await Promise.all([refreshDevices(),refreshAutos(),refreshGrids(),refreshEye(),refreshConnectors(),refreshRelay(),refreshParty()])}
async function boot(){
  try{const s=await api('/api/status');state.wifi=s.wifi||{}}catch{ $('#app').innerHTML='<div class="muted">CORE UNREACHABLE</div>'; return }
  if(state.wifi.sta){
    await refreshDevices();
    await refreshZones();
    await refreshSummary();
    await refreshScenes();
    try{
      const a=await api('/api/arrival');
      state.arrival=!!a.pending;
      if(state.arrival){
        state.arrivalStep='scan';
        try{state.discover=await api('/api/discover/devices')}catch{state.discover={suggestions:[]}}
      }
    }catch{state.arrival=false}
    if(state.tab==='ops') await refreshOpsData();
  }
  render();
}
boot();
setInterval(()=>{if(state.wifi.sta&&!state.forceSetup){refreshDevices();if(state.tab==='home')refreshSummary()}},12000);
</script>
</body>
</html>)HTML";
