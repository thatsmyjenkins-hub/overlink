#pragma once
#include <Arduino.h>

// Grace's Party Pack — phone controller + audience TV page (secrets stay on phone)
static const char GRACE_GAME_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="theme-color" content="#0A1210"/>
<title>Grace's Party Pack</title>
<style>
:root{--bg:#0A1210;--panel:#122018;--cyan:#3DDC97;--amber:#F0A030;--dim:#7A8F80;--active:#E8F5A0;--danger:#e07050;
  --font:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:var(--bg);color:var(--cyan);font-family:var(--font)}
body{display:flex;flex-direction:column;padding:.5rem .55rem calc(.5rem + env(safe-area-inset-bottom));min-height:100dvh}
header{display:flex;justify-content:space-between;align-items:center;gap:.35rem;margin-bottom:.4rem;flex-shrink:0}
h1{margin:0;font-size:.8rem;letter-spacing:.05em}
a.back,button{appearance:none;background:var(--panel);color:var(--cyan);border:1px solid var(--cyan);border-radius:4px;
  padding:.7rem .7rem;font:inherit;font-size:.85rem;letter-spacing:.03em;cursor:pointer;touch-action:manipulation;-webkit-tap-highlight-color:transparent}
button.ok{border-color:var(--active);color:var(--active)}
button.warn{border-color:var(--amber);color:var(--amber)}
button.danger{border-color:var(--danger);color:var(--danger)}
button:disabled{opacity:.35}
button:active{transform:scale(.97)}
.row{display:flex;flex-wrap:wrap;gap:.4rem;margin-top:.4rem}
.row.actions{margin-top:auto;padding-top:.45rem;flex-shrink:0;width:100%}
.row.actions button{flex:1 1 0;min-width:4.2rem;padding:.9rem .4rem;font-size:1rem;font-weight:700}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.4rem}
.tile{padding:1rem .3rem;text-align:center;text-transform:uppercase;font-size:.78rem;line-height:1.15}
.muted{color:var(--dim);font-size:.85rem;line-height:1.4}
.hint{color:var(--dim);font-size:.9rem;line-height:1.4;margin:.35rem 0 .5rem}
.badge{display:inline-block;border:1px solid var(--amber);color:var(--amber);font-size:.7rem;letter-spacing:.06em;
  padding:.2rem .45rem;border-radius:3px;margin-bottom:.35rem}
.prompt{font-size:clamp(2.05rem,8.5vw,2.95rem);color:var(--active);text-align:center;margin:.25rem 0;line-height:1.15;word-break:break-word;font-weight:700}
.prompt.sm{font-size:clamp(1.45rem,6vw,2.05rem)}
.timer{font-size:clamp(1.75rem,6.5vw,2.4rem);color:var(--amber);text-align:center;margin:.3rem 0;letter-spacing:.04em}
.score{color:var(--amber);font-size:1.3rem;text-align:center}
.bans{display:flex;flex-direction:column;align-items:stretch;gap:.4rem;margin:.45rem auto 0;width:100%;max-width:20rem}
.ban{display:block;text-align:center;border:1px solid var(--danger);color:var(--danger);background:rgba(224,112,80,.08);
  padding:.7rem .55rem;border-radius:4px;font-size:clamp(1.2rem,5.2vw,1.65rem);font-weight:700;line-height:1.15}
.panel{border:1px solid var(--cyan);background:var(--panel);border-radius:4px;padding:.75rem;margin-top:.4rem}
.toast{position:fixed;left:.55rem;right:.55rem;bottom:1rem;background:var(--panel);border:1px solid var(--cyan);
  color:var(--active);padding:.7rem .85rem;display:none;z-index:5;font-size:.9rem}
.toast.show{display:block}
select,input{background:#07100e;border:1px solid var(--cyan);color:var(--cyan);padding:.55rem;font:inherit;font-size:.95rem;width:100%;margin:.25rem 0 .45rem}
#app{flex:1;display:flex;flex-direction:column;min-height:0}
.stage{flex:1;display:flex;flex-direction:column;justify-content:center;min-height:0}
.stage.top{justify-content:flex-start;padding-top:.25rem}
.loading{text-align:center;padding:2rem .5rem;color:var(--amber);font-size:1.15rem}
.steps{font-size:.92rem;line-height:1.55;color:var(--cyan);padding-left:1.1rem;margin:.2rem 0}
.steps b{color:var(--active)}
.steps li{margin:.4rem 0}
/* Audience / TV page */
body.present{padding:1rem 1.2rem;justify-content:center}
body.present .chrome{display:none!important}
body.present #app{justify-content:center}
body.present .prompt{font-size:clamp(2.8rem,11vw,6.5rem);margin:.35rem 0 .7rem;line-height:1.1}
body.present .prompt.sm{font-size:clamp(2.1rem,7.5vw,4.2rem)}
body.present .timer{font-size:clamp(2.4rem,6.5vw,3.8rem)}
body.present .score{font-size:clamp(1.5rem,3.8vw,2.4rem)}
body.present .muted{font-size:clamp(1.1rem,2.6vw,1.6rem)}
body.present .pmeta{position:fixed;top:.7rem;left:1rem;right:1rem;display:flex;justify-content:space-between;gap:.5rem;font-size:clamp(1rem,2.3vw,1.35rem);color:var(--dim)}
body.present .stage{justify-content:center;padding:3.2rem 0 1.5rem}
body.present .room-sub{margin-top:.6rem;text-align:center;color:var(--dim);font-size:clamp(1.05rem,2.5vw,1.5rem)}
</style>
</head>
<body>
<header class="chrome">
  <a class="back" href="/">← HOME</a>
  <h1 id="hdr">GRACE'S PARTY PACK</h1>
  <button id="castBtn" class="warn">TV</button>
</header>
<main id="app"></main>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const toast=m=>{const t=$('#toast');t.textContent=m;t.classList.add('show');clearTimeout(toast._t);toast._t=setTimeout(()=>t.classList.remove('show'),2200)};
const params=new URLSearchParams(location.search);
const PRESENT=params.get('present')==='1';
if(PRESENT){document.body.classList.add('present');document.title="Grace · TV"}

/* audience: secret = phone-only card; public/wyr/qa/opp = safe for TV/WallDeck */
const GUIDE={
  taboo:{aud:'secret',room:'GUESS!',sub:'Clue-giver has the phone',how:'Hold the phone so only YOU see it. Describe the green word — never say the red list. Everyone else guesses.'},
  password:{aud:'secret',room:'GUESS!',sub:'One-word clues only',how:'Hold the phone. Give ONE-WORD clues. Room guesses the password. TV stays blank on purpose.'},
  charades:{aud:'secret',room:'WATCH!',sub:'Act it out — no talking',how:'Actor looks at the phone. Act it out, no talking. Room guesses. Keep the phone facing you.'},
  pictionary:{aud:'secret',room:'WATCH!',sub:'Draw it — no letters',how:'Artist looks at the phone. Draw the word (no letters/numbers). Room guesses.'},
  scene:{aud:'secret',room:'WATCH!',sub:'Act the scene',how:'Actors look at the phone. Act out the silly scene. Audience watches — card stays private.'},
  emotions:{aud:'secret',room:'WATCH!',sub:'Act the feeling',how:'Actor looks at the phone. Act out the feeling. Room guesses. Phone faces the actor.'},
  heads:{aud:'public',room:'',sub:'Say what you see',how:'Hold the phone to your forehead so the ROOM sees the word. Team shouts guesses. TV shows the word too.'},
  category:{aud:'public',room:'',sub:'Name things that fit',how:'Everyone sees the category. Take turns naming things that fit before time runs out.'},
  rhyme:{aud:'public',room:'',sub:'Say rhymes out loud',how:'Everyone sees the word. Say as many rhymes as you can.'},
  finish:{aud:'public',room:'',sub:'Finish the sentence',how:'Everyone sees the starter. Finish the sentence out loud.'},
  wyr:{aud:'wyr',room:'',sub:'Pick a side',how:'Everyone sees both options (phone + TV). Pick a side and discuss.'},
  trivia:{aud:'qa',room:'',sub:'Answer, then reveal',how:'Everyone sees the question. Guess out loud, then tap REVEAL on the phone for the answer.'},
  truefalse:{aud:'qa',room:'',sub:'True or false?',how:'Everyone sees the statement. Call true or false, then REVEAL.'},
  opposites:{aud:'opp',room:'',sub:'Say the opposite',how:'Everyone sees the word. Say the opposite, then REVEAL to check.'}
};
function guide(){return GUIDE[S.game?.id]||{aud:'secret',room:'PLAY!',sub:'Follow the phone',how:S.game?.blurb||''}}
function aud(){return guide().aud}

const deckCache=Object.create(null);
let syncTimer=0, syncBusy=false, lastSyncKey='';
const S={
  meta:null, items:[], deal:[], dealIdx:0,
  game:null, screen:'menu',
  timerSec:45, endsAt:0, pausedRemain:0, paused:false,
  teams:true, team:0, scores:[0,0], turnsLeft:6, turnsTotal:6, roundPts:0,
  kids:false, diff:0, cat:0, reveal:false, card:null, tick:null, loading:false
};

async function api(path,opts){
  const r=await fetch(path,opts);
  const t=await r.text();
  let j; try{j=JSON.parse(t)}catch{j={raw:t}}
  if(!r.ok) throw new Error(j.message||t||('HTTP '+r.status));
  return j;
}
function post(path,body){
  return api(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
}

function kidsBlocked(str){
  if(!S.kids||!S.meta) return false;
  const up=String(str||'').toUpperCase();
  return (S.meta.kidsBlock||[]).some(t=>up.includes(t));
}
function cardText(c){
  if(!c) return '';
  if(c.w) return c.w;
  if(c.q) return c.q;
  if(c.a&&c.b&&!c.w) return c.a;
  return '';
}
function passFilter(c){
  if(S.diff && (c.d||0)!==S.diff) return false;
  if(S.cat && (c.c||0)!==S.cat) return false;
  if(kidsBlocked(cardText(c))||(c.b||[]).some(kidsBlocked)||kidsBlocked(c.a)||kidsBlocked(c.q)) return false;
  return true;
}
function shuffle(a){for(let i=a.length-1;i>0;i--){const j=Math.floor(Math.random()*(i+1));[a[i],a[j]]=[a[j],a[i]]} return a}
function armDeal(){
  const idx=[];
  for(let i=0;i<S.items.length;i++) if(passFilter(S.items[i])) idx.push(i);
  S.deal=shuffle(idx); S.dealIdx=0;
}
function nextCard(){
  if(!S.deal.length) return null;
  if(S.dealIdx>=S.deal.length){shuffle(S.deal);S.dealIdx=0}
  return S.items[S.deal[S.dealIdx++]];
}
function remain(){return Math.max(0,S.deal.length-S.dealIdx)}
function remainSec(){return S.endsAt?Math.max(0,Math.ceil((S.endsAt-Date.now())/1000)):S.timerSec}

/** Audience-safe payload for TV page + WallDeck (never leaks secret cards). */
function buildSyncPayload(){
  const g=guide();
  const c=S.card;
  let prompt='', detail='';
  if(S.screen==='play' && c){
    if(g.aud==='secret'){ prompt=g.room||'GUESS!'; detail=g.sub||'Phone has the card' }
    else if(g.aud==='wyr'){ prompt=c.a||''; detail=c.b||'' }
    else if(g.aud==='qa'){ prompt=c.q||''; detail=S.reveal?(c.a||''):'(waiting for reveal)' }
    else if(g.aud==='opp'){ prompt=c.w||''; detail=S.reveal?('→ '+(c.a||'')):'(waiting for reveal)' }
    else { prompt=c.w||cardText(c); detail=g.sub||'' }
  } else if(S.screen==='menu'){ prompt="Grace's Party Pack"; detail='Pick a game on the phone' }
  else if(S.screen==='setup'){ prompt=S.game?.name||'Setup'; detail='Setting up on phone' }
  else if(S.screen==='ready'){ prompt='READY'; detail=S.teams?('Turn · '+(S.team?'BLUE':'RED')):'Free play' }
  else if(S.screen==='buzz'){ prompt='TIME'; detail='Round +'+S.roundPts }
  else if(S.screen==='win'){ prompt=(S.scores[0]===S.scores[1]?'TIE':(S.scores[0]>S.scores[1]?'RED':'BLUE'))+' WINS'; detail='' }
  else if(S.screen==='cast'){ prompt='TV setup'; detail='Cast the TV page — not Play' }
  return {
    gameId:S.game?.id||'', gameName:S.game?.name||"Grace's Party Pack",
    screen:S.screen, prompt, detail, blurb:S.game?.blurb||'',
    teamLabel:S.teams?(S.team?'BLUE':'RED'):'FREE',
    scores0:S.scores[0], scores1:S.scores[1],
    timerSec:S.timerSec, remainSec:remainSec(), reveal:S.reveal, kids:S.kids
  };
}
function syncState(force){
  if(PRESENT) return;
  const p=buildSyncPayload();
  const key=JSON.stringify(p);
  if(!force && key===lastSyncKey) return;
  lastSyncKey=key;
  clearTimeout(syncTimer);
  syncTimer=setTimeout(async()=>{
    if(syncBusy){syncTimer=setTimeout(()=>syncState(true),80);return}
    syncBusy=true;
    try{await post('/api/games/grace/state',p)}catch(_){}
    syncBusy=false;
  },40);
}

function stopTick(){if(S.tick){clearInterval(S.tick);S.tick=null}}
function startTick(){
  stopTick();
  S.tick=setInterval(()=>{
    if(S.paused||!S.endsAt) return;
    if(Date.now()>=S.endsAt){S.screen='buzz';stopTick();render();return}
    const el=$('#timer'); const r=remainSec();
    if(el) el.textContent=String(r);
    const now=Date.now();
    if(!S._lastSyncAt || now-S._lastSyncAt>900){S._lastSyncAt=now; syncState()}
  },200);
}

async function loadMeta(){
  let last;
  for(let i=0;i<4;i++){
    try{S.meta=await api('/games/grace/meta.json'); return}
    catch(e){last=e; await new Promise(r=>setTimeout(r,250+i*300))}
  }
  throw last||new Error('meta');
}
async function loadDeck(id){
  if(deckCache[id]){S.items=deckCache[id]; armDeal(); return}
  let j=await api('/games/grace/decks/'+id+'.json');
  if(j.alias){
    const aid=j.alias;
    if(deckCache[aid]){S.items=deckCache[aid]; deckCache[id]=S.items; armDeal(); return}
    j=await api('/games/grace/decks/'+aid+'.json');
    deckCache[aid]=j.items||[];
    S.items=deckCache[aid];
    deckCache[id]=S.items;
  } else {
    S.items=j.items||[];
    deckCache[id]=S.items;
  }
  armDeal();
}

function openPresenter(){
  window.open('/games/grace/?present=1','_blank','noopener');
  toast('TV page opened — cast THAT tab, keep Play on phone');
}

function renderMenu(){
  const games=(S.meta.games||[]).map(g=>`<button class="tile" data-g="${esc(g.id)}">${esc(g.name)}</button>`).join('');
  return `<div class="muted">Phone = controller · TV shows only what the room should see</div>
    <div class="grid" style="margin-top:.45rem">${games}</div>
    <div class="panel chrome">
      <div class="row">
        <button id="kids" class="${S.kids?'ok':''}">${S.kids?'KIDS ON':'KIDS OFF'}</button>
        <button id="present" class="warn">OPEN TV PAGE</button>
      </div>
      <div class="muted" style="margin-top:.55rem">Tap <b style="color:var(--amber)">TV</b> for cast steps. Secret cards (Don't Say It, Charades…) stay on this phone.</div>
    </div>`;
}
function renderSetup(){
  const g=guide();
  const cats=(S.meta.cats||[]).map((c,i)=>`<option value="${i}" ${S.cat===i?'selected':''}>${esc(c)}</option>`).join('');
  const diffs=(S.meta.diffs||[]).map((c,i)=>`<option value="${i}" ${S.diff===i?'selected':''}>${esc(c)}</option>`).join('');
  const vis=g.aud==='secret'
    ? 'TV / WallDeck: hides the card (shows GUESS/WATCH + timer)'
    : 'TV / WallDeck: shows the same prompt as the room';
  return `<div class="hint">${esc(g.how)}</div>
    <div class="muted" style="margin-bottom:.45rem">${esc(vis)}</div>
    <div class="panel">
      <label class="muted">timer</label>
      <div class="row">${[30,45,60,90].map(t=>`<button data-t="${t}" class="${S.timerSec===t?'ok':''}">${t}s</button>`).join('')}</div>
      <label class="muted">difficulty</label><select id="diff">${diffs}</select>
      <label class="muted">category</label><select id="cat">${cats}</select>
      <div class="row" style="margin-top:.35rem">
        <button id="teams" class="${S.teams?'ok':''}">${S.teams?'TEAMS ON':'TEAMS OFF'}</button>
        <span class="muted" style="align-self:center">${remain()||S.deal.length} cards</span>
      </div>
    </div>
    <div class="row actions chrome"><button id="backMenu">BACK</button><button id="start" class="ok" ${!S.deal.length?'disabled':''}>START</button></div>`;
}
function renderReady(){
  const g=guide();
  return `<div class="stage"><div class="prompt">READY</div>
    <div class="muted" style="text-align:center;font-size:1rem">${S.teams?`Turn · ${S.team?'BLUE':'RED'} · ${S.turnsLeft} left`:'Free play'} · ${remain()} cards</div>
    <div class="hint" style="text-align:center">${esc(g.how)}</div>
    <div class="score" style="margin-top:.4rem">RED ${S.scores[0]} · BLUE ${S.scores[1]}</div></div>
    <div class="row actions chrome"><button id="backSetup">BACK</button><button id="go" class="ok">GO</button></div>`;
}
function cardBodyHtml(){
  const mode=S.game.mode, c=S.card||{}, g=guide();
  const badge=g.aud==='secret'?`<div class="badge">PHONE ONLY — hide from room</div>`:`<div class="badge" style="border-color:var(--cyan);color:var(--cyan)">ROOM CAN SEE THIS</div>`;
  if(mode==='bans'){
    return `${badge}<div class="prompt">${esc(c.w||'')}</div>
      <div class="muted" style="text-align:center;margin:.15rem 0 .2rem">don't say</div>
      <div class="bans">${(c.b||[]).map(x=>`<span class="ban">${esc(x)}</span>`).join('')}</div>`;
  }
  if(mode==='wyr') return `${badge}<div class="prompt sm">${esc(c.a||'')}</div><div class="muted" style="text-align:center;font-size:1.15rem;margin:.35rem 0">OR</div><div class="prompt sm">${esc(c.b||'')}</div>`;
  if(mode==='qa'||mode==='opp'){
    const main=mode==='opp'?c.w:c.q;
    const ans=c.a;
    return `${badge}<div class="prompt">${esc(main||'')}</div>${S.reveal?`<div class="muted" style="text-align:center;color:var(--amber);font-size:clamp(1.25rem,5vw,1.7rem);margin-top:.55rem">${esc(ans||'')}</div>`:''}`;
  }
  return `${badge}<div class="prompt">${esc(c.w||'')}</div>`;
}
function renderPlay(){
  const mode=S.game.mode;
  const top=mode==='bans'?' top':'';
  return `<div class="stage${top}" id="cardBody">${cardBodyHtml()}</div>
    <div class="timer" id="timer">${remainSec()}</div>
    <div class="muted" style="text-align:center" id="scoreLine">RED ${S.scores[0]} · BLUE ${S.scores[1]}${S.teams?` · +${S.roundPts}`:''}</div>
    <div class="row actions chrome">
      <button id="skip" class="warn">SKIP</button>
      ${(mode==='qa'||mode==='opp')?`<button id="reveal" class="ok">${S.reveal?'HIDE':'REVEAL'}</button>`:''}
      <button id="got" class="ok">GOT IT</button>
      <button id="more">…</button>
    </div>`;
}
function renderBuzz(){
  return `<div class="stage"><div class="prompt">TIME</div>
    <div class="muted" style="text-align:center;font-size:1.1rem">round points: ${S.roundPts}</div>
    <div class="score" style="margin-top:.6rem">RED ${S.scores[0]} · BLUE ${S.scores[1]}</div></div>
    <div class="row actions chrome"><button id="nextTurn" class="ok">NEXT</button></div>`;
}
function renderWin(){
  const winner=S.scores[0]===S.scores[1]?'TIE':(S.scores[0]>S.scores[1]?'RED':'BLUE');
  return `<div class="stage"><div class="prompt">${winner} WINS</div>
    <div class="score" style="margin-top:.6rem">RED ${S.scores[0]} · BLUE ${S.scores[1]}</div></div>
    <div class="row actions chrome"><button id="again" class="ok">MENU</button></div>`;
}
function renderCast(){
  return `<div class="panel">
    <h1 style="font-size:1.15rem;margin:0 0 .55rem;color:var(--amber)">SHOW THE ROOM (NOT THE SECRETS)</h1>
    <ol class="steps">
      <li><b>Play stays on this phone</b> — that is the controller. Secret cards (Don't Say It, Charades, Password…) never go to the TV.</li>
      <li>Tap <b>OPEN TV PAGE</b>. A second tab opens — <b>cast that tab only</b>.</li>
      <li><b>iPhone:</b> Control Center → Screen Mirroring → your TV / Apple TV.</li>
      <li><b>Android / Chrome:</b> menu → Cast → your TV → choose <b>Cast tab</b>.</li>
      <li><b>No cast?</b> WallDeck → Home → <b>GRACE PARTY</b> shows the same room view (timer + scores; hides secret words).</li>
    </ol>
    <div class="muted" style="margin-top:.55rem">Heads Up, Trivia, Would You Rather, etc. show on the TV because the whole room should see them.</div>
    <div class="row" style="margin-top:.75rem">
      <button id="openPresent" class="ok">OPEN TV PAGE</button>
      <button id="closeCast">CLOSE</button>
    </div>
  </div>`;
}
function renderLoading(name){
  return `<div class="loading">Loading ${esc(name||'deck')}…</div>`;
}

function paintCardFast(){
  const body=$('#cardBody'), timer=$('#timer');
  if(!body || S.screen!=='play'){render(); return}
  body.innerHTML=cardBodyHtml();
  if(timer) timer.textContent=String(remainSec());
  syncState(true);
}

function render(){
  if(PRESENT) return;
  const h=$('#hdr');
  if(h) h.textContent=(S.game&&S.screen!=='menu'&&S.screen!=='cast')?S.game.name.toUpperCase():"GRACE'S PARTY PACK";
  let html='';
  if(S.loading) html=renderLoading(S.game?.name);
  else if(S.screen==='menu') html=renderMenu();
  else if(S.screen==='setup') html=renderSetup();
  else if(S.screen==='ready') html=renderReady();
  else if(S.screen==='play') html=renderPlay();
  else if(S.screen==='buzz') html=renderBuzz();
  else if(S.screen==='win') html=renderWin();
  else if(S.screen==='cast') html=renderCast();
  $('#app').innerHTML=html;
  bind();
  syncState(true);
}

function beginPlay(){
  S.card=nextCard();
  S.roundPts=0; S.reveal=false; S.paused=false;
  S.endsAt=Date.now()+S.timerSec*1000;
  S.screen='play';
  startTick();
  render();
}

function bind(){
  document.querySelectorAll('[data-g]').forEach(b=>b.onclick=async()=>{
    S.game=S.meta.games.find(g=>g.id===b.dataset.g);
    S.loading=true; S.screen='setup'; render();
    try{
      await loadDeck(S.game.deck);
      S.loading=false; render();
    }catch(e){
      S.loading=false; S.screen='menu'; S.game=null; render();
      toast('deck missing — run push_grace_games');
      console.error(e);
    }
  });
  const kids=$('#kids'); if(kids) kids.onclick=()=>{S.kids=!S.kids; if(S.game) armDeal(); render()};
  const present=$('#present'); if(present) present.onclick=()=>openPresenter();
  const castBtn=$('#castBtn'); if(castBtn) castBtn.onclick=()=>{S.screen='cast';render()};
  const openPresent=$('#openPresent'); if(openPresent) openPresent.onclick=()=>openPresenter();
  const closeCast=$('#closeCast'); if(closeCast) closeCast.onclick=()=>{S.screen='menu';render()};
  document.querySelectorAll('[data-t]').forEach(b=>b.onclick=()=>{S.timerSec=+b.dataset.t;render()});
  const diff=$('#diff'); if(diff) diff.onchange=()=>{S.diff=+diff.value;armDeal();render()};
  const cat=$('#cat'); if(cat) cat.onchange=()=>{S.cat=+cat.value;armDeal();render()};
  const teams=$('#teams'); if(teams) teams.onclick=()=>{S.teams=!S.teams;render()};
  const backMenu=$('#backMenu'); if(backMenu) backMenu.onclick=()=>{S.screen='menu';S.game=null;render()};
  const start=$('#start'); if(start) start.onclick=()=>{S.scores=[0,0];S.team=0;S.turnsLeft=S.turnsTotal;S.screen='ready';render()};
  const backSetup=$('#backSetup'); if(backSetup) backSetup.onclick=()=>{S.screen='setup';render()};
  const go=$('#go'); if(go) go.onclick=()=>beginPlay();
  const skip=$('#skip'); if(skip) skip.onclick=()=>{S.card=nextCard();S.reveal=false;paintCardFast()};
  const got=$('#got'); if(got) got.onclick=()=>{
    S.roundPts++; if(S.teams) S.scores[S.team]++;
    S.card=nextCard(); S.reveal=false;
    const sc=$('#scoreLine'); if(sc) sc.textContent=`RED ${S.scores[0]} · BLUE ${S.scores[1]}${S.teams?` · +${S.roundPts}`:''}`;
    paintCardFast();
  };
  const reveal=$('#reveal'); if(reveal) reveal.onclick=()=>{S.reveal=!S.reveal;paintCardFast()};
  const more=$('#more'); if(more) more.onclick=()=>{
    stopTick(); S.paused=true; S.pausedRemain=Math.max(0,S.endsAt-Date.now());
    if(confirm('End round / back to ready?')){S.screen='ready';S.endsAt=0;render()}
    else {S.paused=false; S.endsAt=Date.now()+S.pausedRemain; startTick()}
  };
  const nextTurn=$('#nextTurn'); if(nextTurn) nextTurn.onclick=()=>{
    if(S.teams){S.turnsLeft--; S.team^=1; if(S.turnsLeft<=0){S.screen='win';render();return}}
    S.screen='ready';render();
  };
  const again=$('#again'); if(again) again.onclick=()=>{S.screen='menu';S.game=null;render()};
}

/* —— TV / audience page: only shows Core session (already audience-safe) —— */
let presentKey='';
function presentHtml(j){
  const screen=j.screen||'menu';
  const id=j.gameId||'';
  const prompt=j.prompt||"Grace's Party Pack";
  const detail=j.detail||'';
  const team=j.teamLabel||'';
  const name=j.gameName||"Grace's Party Pack";
  const g=GUIDE[id]||{};
  let mid='';
  if(screen==='play'){
    if(g.aud==='wyr'){
      mid=`<div class="prompt sm">${esc(prompt)}</div><div class="muted" style="text-align:center;margin:.45rem 0">OR</div><div class="prompt sm">${esc(detail)}</div>`;
    } else {
      mid=`<div class="prompt">${esc(prompt)}</div>`;
      if(detail) mid+=`<div class="room-sub">${esc(detail)}</div>`;
    }
    mid+=`<div class="timer">${esc(String(j.remainSec??j.timerSec??''))}</div>`;
  } else {
    mid=`<div class="prompt">${esc(prompt)}</div>`;
    if(detail) mid+=`<div class="room-sub">${esc(detail)}</div>`;
  }
  return `<div class="pmeta"><span>${esc(name)}</span><span>${esc(team)} · R ${j.scores0|0} · B ${j.scores1|0}</span></div>
    <div class="stage">${mid}</div>`;
}
async function presentPoll(){
  try{
    const j=await api('/api/games/grace/state');
    const key=j.updatedMs+'|'+j.prompt+'|'+j.detail+'|'+j.screen+'|'+j.remainSec+'|'+j.scores0+'|'+j.scores1+'|'+j.reveal+'|'+j.gameId;
    if(key===presentKey) return;
    presentKey=key;
    $('#app').innerHTML=presentHtml(j);
  }catch(_){}
}

async function warmDecks(){
  const warm=['taboo','charades','wyr','trivia','heads'];
  for(const id of warm){
    if(deckCache[id]) continue;
    try{
      const j=await api('/games/grace/decks/'+id+'.json');
      if(j.items) deckCache[id]=j.items;
    }catch(_){}
    await new Promise(r=>setTimeout(r,120));
  }
}
async function boot(){
  if(PRESENT){
    $('#app').innerHTML=`<div class="loading">Waiting for phone…</div>
      <div class="muted" style="text-align:center;margin-top:1rem">Cast <b>this</b> tab to the TV.<br/>Keep Games → PLAY open on the phone as controller.</div>`;
    await presentPoll();
    setInterval(presentPoll, 550);
    return;
  }
  $('#app').innerHTML=`<div class="loading">Loading games…</div>`;
  try{
    await loadMeta();
    render();
    setTimeout(warmDecks, 400);
  }catch(e){
    console.error(e);
    $('#app').innerHTML=`<div class="panel"><div class="muted">Could not load game list from Core.</div>
      <div class="muted" style="margin-top:.5rem">Check Wi‑Fi, then retry.</div>
      <div class="row" style="margin-top:.6rem"><button id="retryBoot" class="ok">RETRY</button><a class="back" href="/">← HOME</a></div></div>`;
    const rb=$('#retryBoot'); if(rb) rb.onclick=()=>boot();
  }
}
boot();
</script>
</body>
</html>)HTML";
