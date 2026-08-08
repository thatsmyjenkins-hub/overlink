/* CYBERDECK NET INTEL — client */
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];

const TAB_GUIDES = {
  home: {
    title: 'HOME — Orchestration & intel',
    blurb: 'Control the deck remotely, run playbooks, share exports, and monitor live LAN/Wi‑Fi data.',
    steps: [
      'Deck Control — trigger LAN/RF scans, profile hosts, pause auto cycles, or clear data.',
      'Playbooks chain commands (e.g. Full recon = sweep → profile → export).',
      'Share and Applications below; tap a host row for forensics.',
    ],
    tips: 'Pause auto scans before a manual LAN sweep so the deck focuses on your command.',
  },
  spectrum: {
    title: 'SPECTRUM — Wireless RF Intelligence',
    blurb: 'Maps nearby Wi-Fi: SSIDs, signal strength, channels, and encryption. Flags open or weak networks.',
    steps: [
      'RF scan runs about every 30 seconds when the deck is not busy with a LAN sweep (~5–8 s per scan).',
      'Channel map — bar height shows how many APs share each channel (congestion indicator).',
      'AP table — sorted by risk. OPEN/WEP networks score highest; hidden SSIDs are labeled.',
      'Tap any AP row to open forensics — signal profile, channel audit, SSID collision, security audit, rogue check.',
    ],
    tips: 'You are connected to one AP while scanning others — counts may include your own SSID and neighbors.',
  },
  netmap: {
    title: 'NETMAP — LAN Host Radar',
    blurb: 'mDNS/SSDP passive discovery plus TCP sweeps. Hosts can appear within seconds if they advertise Bonjour/UPnP.',
    steps: [
      'Passive — deck listens for mDNS (.local) and SSDP (smart TVs, printers) while connected.',
      'LAN_SWEEP — priority IPs first, then subnet TCP on 80, 443, 22, 445.',
      'Between sweeps — slow ARP finds quiet devices (phones) without open ports.',
      'Host table — name column when mDNS/SSDP provided one; tap row for forensics.',
    ],
    tips: 'discoveredBy in export: mdns, ssdp, tcp, or arp. Quiet phones may take a minute via ARP gap-fill.',
  },
  phantom: {
    title: 'PHANTOM — Threat Surface Analyzer',
    blurb: 'Prioritizes exposure: risky hosts, open nearby Wi-Fi, and aggregate scores for quick triage.',
    steps: [
      'MAX RISK — highest host score from open services (Telnet, SMB, HTTP without HTTPS, etc.).',
      'OPEN APs — wireless networks with no encryption in RF range (from last SPECTRUM scan).',
      'CRITICAL HOSTS — endpoints scoring 70+; tap a card to drill into port-level forensics.',
      'Use with SPECTRUM to separate “bad Wi-Fi nearby” vs “bad services on your LAN”.',
    ],
    tips: 'Risk scores are heuristics from open ports and Wi-Fi encryption — not a full audit.',
  },
  profiles: {
    title: 'PROFILES — Device Identity & CVE Intel',
    blurb: 'First-class fingerprinting: MAC/OUI vendor, HTTP banners, device class, OS guess, and matched public CVEs.',
    steps: [
      'After a host is found, the deck queues PROFILE — ARP for MAC, IEEE OUI vendor lookup, HTTP banner grab.',
      'Fleet table shows vendor, device type, OS estimate, confidence %, and curated CVE hit count.',
      'Tap a row for full forensics plus known vulnerabilities from the on-board rules database.',
      '“Search CIRCL” queries the free CIRCL CVE API (MITRE/NVD-backed) for additional live results.',
    ],
    tips: 'Curated CVEs + EXPOSURE-* rules are pattern-matched — not a full pentest. Patch what you own.',
  },
  ap: {
    title: 'AP FORENSICS — Wireless Drill-Down',
    blurb: 'Passive pen-test scenarios on one BSSID: RSSI stability, co-channel neighbors, evil-twin detection, encryption posture, rogue comparison.',
    steps: [
      'Identity — SSID, BSSID, vendor (OUI), channel, band, and heuristic risk score.',
      'Scenarios — each action runs on the deck (no deauth); results appear below with severity findings.',
      'Passive recon bundles all checks in one run (~10–15 s).',
      'RF rescan refreshes the global AP table — reload this page after it completes.',
    ],
    tips: 'Legal use only on networks you own or are authorized to assess. ESP32 cannot crack WPA — these are recon aids.',
  },
  host: {
    title: 'HOST — Endpoint tools & forensics',
    blurb: 'Tap a device row to open tools. Forensics (profiling/CVE) run only when you choose — not automatic unless you enable auto-profile on HOME.',
    steps: [
      'Run forensics — queues ARP, banner grab, and on-device CVE matching on the ESP32.',
      'Load CVE report — refreshes cached profile/CVE data in the browser (no new deck scan).',
      'CVE search — browser query to CIRCL; optional and separate from deck profiling.',
      'Enable auto-profile on HOME only if you want every new host profiled automatically.',
    ],
    tips: 'Deck control bar (top) shows pause/stop for LAN sweep and profile queue.',
  },
};

let state = {
  view: 'home',
  summary: {},
  wifi: { aps: [] },
  hosts: { hosts: [] },
  profiles: { profiles: [] },
  events: { events: [] },
  selectedHost: null,
  selectedAp: null,
  apActionBusy: false,
  hostActionBusy: false,
  hostForensicsLoaded: false,
  hostLiveRefresh: false,
  control: {},
  stack: ['home'],
  showGuides: sessionStorage.getItem('cyd_guides') === '1',
};

async function apiGet(path) {
  const r = await fetch(path, { cache: 'no-store' });
  if (!r.ok) throw new Error(`${path} HTTP ${r.status}`);
  return r.json();
}

const API = {
  all: async () => {
    const r = await fetch('/api/intel/all', { cache: 'no-store' });
    if (r.status === 503) return { busy: true };  // deck briefly low on memory; keep prior data
    if (!r.ok) throw new Error('/api/intel/all HTTP ' + r.status);
    return r.json();
  },
  host: ip => apiGet('/api/intel/host?ip=' + encodeURIComponent(ip)),
  ap: bssid => apiGet('/api/intel/ap?bssid=' + encodeURIComponent(bssid)),
  apAction: (bssid, action) => {
    const q = new URLSearchParams({ bssid, action });
    return fetch('/api/intel/ap/action?' + q, { method: 'POST', cache: 'no-store' })
      .then(async r => {
        const j = await r.json();
        if (!r.ok && !j.message) throw new Error(`ap action ${r.status}`);
        return j;
      });
  },
  exportBundle: (refresh = false) =>
    fetch('/api/intel/export' + (refresh ? '?refresh=1' : ''), { cache: 'no-store' })
      .then(r => { if (!r.ok) throw new Error('export ' + r.status); return r.json(); }),
  reportText: (refresh = false) =>
    fetch('/api/intel/report' + (refresh ? '?refresh=1' : ''), { cache: 'no-store' })
      .then(r => { if (!r.ok) throw new Error('report ' + r.status); return r.text(); }),
  controlStatus: () => apiGet('/api/control/status'),
  health: () => apiGet('/api/health'),
  control: (action, ip = '') => {
    const q = new URLSearchParams({ action });
    if (ip) q.set('ip', ip);
    return fetch('/api/control?' + q, { method: 'POST', cache: 'no-store' })
      .then(async r => {
        const j = await r.json();
        if (!r.ok && !j.message) throw new Error(`control ${r.status}`);
        return j;
      });
  },
};

const AP_SCENARIOS = [
  { action: 'security_audit', label: 'Security audit', hint: 'Encryption & hidden SSID' },
  { action: 'signal_profile', label: 'Signal profile', hint: '5-sample RSSI on deck' },
  { action: 'channel_audit', label: 'Channel audit', hint: 'Co-channel neighbors' },
  { action: 'ssid_collision', label: 'SSID collision', hint: 'Evil-twin / duplicate SSID' },
  { action: 'pentest_recon', label: 'Passive recon', hint: 'All AP checks bundled' },
];

const HOST_ACTIONS = [
  { id: 'forensics', label: 'Run forensics', hint: 'ARP, HTTP banner, CVE rules on deck' },
  { id: 'load', label: 'Load CVE report', hint: 'Fetch full profile from deck cache' },
  { id: 'circl', label: 'CVE search (CIRCL)', hint: 'Live MITRE/NVD lookup in browser' },
  { id: 'lanmap', label: 'Open in LAN map', hint: 'Jump to radar view' },
];

const CONTROL_ACTIONS = [
  { action: 'lan_sweep', label: 'LAN sweep', hint: 'Probe subnet now' },
  { action: 'rf_scan', label: 'RF scan', hint: 'Refresh nearby APs' },
  { action: 'profile_all', label: 'Profile all', hint: 'Queue every host' },
  { action: 'export', label: 'Save snapshot', hint: 'JSON + report on deck' },
  { action: 'pause', label: 'Pause auto', hint: 'Stop background cycles' },
  { action: 'resume', label: 'Resume auto', hint: 'Re-enable cycles' },
  { action: 'clear_discovery', label: 'Clear hosts', hint: 'Reset discovery tables', danger: true },
  { action: 'clear_log', label: 'Clear log', hint: 'Wipe event log', danger: true },
];

let orchestrating = false;

const APP_TILES = [
  { view: 'home', title: 'Overview', desc: 'Hosts, Wi‑Fi, alerts, log', icon: '◉' },
  { view: 'netmap', title: 'LAN Map', desc: 'Radar + host table', icon: '◎' },
  { view: 'spectrum', title: 'Wi‑Fi', desc: 'APs & channels', icon: '≋' },
  { view: 'profiles', title: 'Devices', desc: 'Vendors & CVEs', icon: '▣' },
  { view: 'phantom', title: 'Threats', desc: 'Risk prioritization', icon: '⚠' },
];

function renderPortalBanner(health) {
  let el = $('#portal-banner');
  if (!health?.portalActive) {
    if (el) el.remove();
    return;
  }
  if (!el) {
    el = document.createElement('div');
    el.id = 'portal-banner';
    el.className = 'portal-banner';
    const app = $('#app');
    const header = app?.querySelector('header.top');
    if (header?.nextSibling) header.parentNode.insertBefore(el, header.nextSibling);
    else if (app) app.prepend(el);
  }
  const url = health.portalUrl || 'http://192.168.4.1/';
  el.innerHTML = `<strong>Setup mode</strong> — Join Wi‑Fi <code>${esc(health.portalSsid || 'CYBERDECK')}</code> on this device. Your phone should open the dashboard automatically; if not, open <a href="${esc(url)}">${esc(url)}</a>.`;
}

function setLinkStatus(ok, msg) {
  const badge = $('#live-badge');
  const tick = $('#ticker');
  if (badge) {
    badge.textContent = ok ? 'LIVE' : 'OFFLINE';
    badge.style.borderColor = ok ? 'var(--cyan)' : 'var(--danger)';
    badge.style.color = ok ? 'var(--cyan)' : 'var(--danger)';
  }
  if (!ok && tick) tick.textContent = msg;
}

function riskClass(r) {
  if (r >= 70) return 'risk-high';
  if (r >= 40) return 'risk-med';
  return 'risk-low';
}

function phaseBadgeClass(phase) {
  if (phase === 'LAN_SWEEP' || phase === 'RF_SCAN' || phase === 'PROFILE') return 'phase-active';
  if (phase === 'MONITOR') return 'phase-idle';
  if (phase === 'NO_LINK') return 'phase-warn';
  return '';
}

function fmtSec(s) {
  if (s == null || s <= 0) return 'now';
  if (s < 60) return s + 's';
  return Math.floor(s / 60) + 'm ' + (s % 60) + 's';
}

function guideHtml(key) {
  const g = TAB_GUIDES[key];
  if (!g || !state.showGuides) return '';
  if (key === 'home' && state.view === 'home') return '';
  const steps = g.steps.map((t, i) => `<li><span class="step-n">${i + 1}</span>${esc(t)}</li>`).join('');
  return `
    <div class="guide-panel">
      <div class="guide-head">
        <h2>${esc(g.title)}</h2>
        <p class="guide-blurb">${esc(g.blurb)}</p>
      </div>
      <ol class="guide-steps">${steps}</ol>
      <p class="guide-tip"><strong>TIP</strong> ${esc(g.tips)}</p>
    </div>`;
}

function progressBarMeta(s) {
  const phase = s.phase || '';
  const kind = s.progressKind || (phase === 'LAN_SWEEP' ? 'lan' : phase === 'RF_SCAN' ? 'rf' : phase === 'PROFILE' ? 'profile' : 'idle');
  const active = kind === 'lan' || kind === 'rf' || kind === 'profile';
  const pct = active ? (s.progress ?? 0) : (s.idleCountdownPct ?? 0);
  let barTitle = s.progressLabel || s.phaseLabel || phase;
  if (kind === 'lan') barTitle = s.progressLabel || `LAN ${s.lanChecked ?? s.lanIndex ?? 0}/${s.lanTotal ?? 253}`;
  if (kind === 'idle') barTitle = s.progressLabel || 'Countdown to next LAN sweep';
  return { kind, pct, barTitle, active };
}

function renderScanStatus() {
  const el = $('#scan-status');
  if (!el) return;
  const s = state.summary;
  const phase = s.phase || '—';
  const { kind, pct, barTitle } = progressBarMeta(s);
  const active = phase === 'LAN_SWEEP' || phase === 'RF_SCAN' || phase === 'PROFILE';

  let task = s.phaseLabel || phase;
  if (phase === 'LAN_SWEEP' && s.scanTarget) {
    const pIdx = s.portProbeIdx ?? '?';
    task = `Probing ${s.scanTarget}:${s.scanPort || '?'} (port rotation ${pIdx}/4)`;
  } else if (phase === 'RF_SCAN') {
    task = 'Blocking Wi-Fi scan — nearby APs (~5–8s)';
  } else if (phase === 'PROFILE') {
    task = s.progressLabel || `Profiling devices (${s.profileQueue ?? 0} queued)`;
  } else if (phase === 'MONITOR') {
    task = s.progressLabel || 'Between sweeps';
    const parts = [];
    if (s.nextLanSec > 0) parts.push(`LAN in ${fmtSec(s.nextLanSec)}`);
    if (s.nextRfSec > 0) parts.push(`RF in ${fmtSec(s.nextRfSec)}`);
    if (parts.length) task += ' · ' + parts.join(' · ');
  }

  const rfFresh = s.wifiCount > 0
    ? (s.rfAgeSec < 35 ? 'fresh' : 'stale')
    : 'pending';

  const barLeft = kind === 'lan' ? 'LAN sweep'
    : kind === 'rf' ? 'RF scan'
    : kind === 'profile' ? 'Profiling'
    : 'Until next sweep';

  el.innerHTML = `
    <div class="scan-grid">
      <div class="scan-main">
        <span class="phase-pill ${phaseBadgeClass(phase)}">${esc(phase)}</span>
        <span class="scan-task">${esc(task)}</span>
      </div>
      <div class="scan-bar-label">
        <span title="${esc(barTitle)}">${esc(barLeft)} · ${esc(barTitle)}</span>
        <span>${pct}%</span>
      </div>
      <div class="scan-metrics">
        <div class="metric" title="Hosts discovered vs subnet slots checked this sweep (not all devices respond to probes)">
          <span class="metric-val">${s.hitRatePct ?? s.discoveryPct ?? 0}%</span>
          <span class="metric-lbl">Host hit rate</span>
        </div>
        <div class="metric" title="Completed LAN sweeps since boot">
          <span class="metric-val">${s.sweepCount ?? 0}</span>
          <span class="metric-lbl">Sweeps done</span>
        </div>
        <div class="metric" title="Subnet slots evaluated (.1–.254 model)">
          <span class="metric-val">${s.lanChecked ?? s.lanIndex ?? 0}/${s.lanTotal ?? 253}</span>
          <span class="metric-lbl">LAN slots</span>
        </div>
        <div class="metric" title="Hosts fully profiled (banner/CVE rules)">
          <span class="metric-val">${s.profiledHosts ?? 0}/${s.hostCount ?? 0}</span>
          <span class="metric-lbl">Profiled</span>
        </div>
        <div class="metric" title="Seconds since last RF scan">
          <span class="metric-val ${rfFresh === 'stale' ? 'risk-med' : ''}">${s.rfAgeSec ?? '—'}s</span>
          <span class="metric-lbl">RF age</span>
        </div>
        <div class="metric" title="Wi-Fi APs in last RF scan">
          <span class="metric-val">${s.wifiCount ?? 0}</span>
          <span class="metric-lbl">APs mapped</span>
        </div>
      </div>
    </div>
    ${active ? '<div class="scan-pulse" aria-hidden="true"></div>' : ''}`;
}

function deckBusyFlags(s) {
  const phase = s.phase || '';
  return {
    lan: phase === 'LAN_SWEEP' || s.sweepActive,
    rf: phase === 'RF_SCAN' || s.rfInProgress,
    profile: phase === 'PROFILE' || (s.profileQueue || 0) > 0,
    paused: s.orchestrationPaused,
    autoProfile: s.autoProfile,
    multicast: s.multicastListen,
  };
}

// Read-only live indicator. running => green (pulsing), idle => gray.
function liveChip(name, running, extra = '') {
  return `<span class="st-chip ${running ? 'st-run' : 'st-idle'}">
    <b>${name}</b> ${running ? '\u25CF running' + extra : 'idle'}</span>`;
}

// Consistent on/off (or on/paused) toggle. The whole control is colored by its
// CURRENT state and shows an explicit state badge, so it's never ambiguous.
//   on=true  -> green "ON"
//   on=false -> amber "PAUSED" (offState 'paused') or gray "OFF" (offState 'off')
function toggleOp(name, on, dataOps, offState) {
  const cls = on ? 'st-on' : (offState === 'paused' ? 'st-paused' : 'st-off');
  const stateTxt = on ? 'ON' : (offState === 'paused' ? 'PAUSED' : 'OFF');
  const verb = on ? (offState === 'paused' ? 'pause' : 'turn off') : (offState === 'paused' ? 'resume' : 'turn on');
  return `<button type="button" class="op-toggle ${cls}" data-ops="${dataOps}" aria-pressed="${on}" title="Tap to ${verb} ${name}">
    <span class="op-toggle-name">${name}</span>
    <span class="op-toggle-state">${stateTxt}</span>
  </button>`;
}

function renderOpsBar() {
  const el = $('#ops-bar');
  if (!el) return;
  const s = state.summary;
  const f = deckBusyFlags(s);
  const q = s.profileQueue || 0;
  const reconOn = !f.paused;

  el.innerHTML = `
    <span class="ops-label">DECK</span>
    ${liveChip('LAN', f.lan)}
    ${liveChip('RF', f.rf)}
    ${liveChip('Profile', f.profile, q ? ' (' + q + ')' : '')}
    <span class="st-chip ${f.multicast ? 'st-on' : 'st-off'}">
      <b>mDNS/SSDP</b> ${f.multicast ? 'on' : 'off'}</span>
    ${toggleOp('Auto-recon', reconOn, reconOn ? 'pause' : 'resume', 'paused')}
    ${toggleOp('Auto-profile', !!s.autoProfile, s.autoProfile ? 'auto_profile_off' : 'auto_profile_on', 'off')}
    <button type="button" data-ops="cancel_sweep" class="op-action" ${f.lan ? '' : 'disabled'}>Stop LAN</button>
    <button type="button" data-ops="stop_profile" class="op-action" ${f.profile ? '' : 'disabled'}>Stop profile</button>`;

  el.querySelectorAll('[data-ops]').forEach(btn => {
    btn.onclick = () => runDeckOp(btn.dataset.ops, btn);
  });
}

async function runDeckOp(action, btn) {
  if (btn) btn.disabled = true;
  try {
    const res = await API.control(action);
    setOrchStatus(res.message || action, res.ok !== false);
    await refresh();
  } catch (e) {
    setOrchStatus(e.message, false);
  } finally {
    if (btn) btn.disabled = false;
  }
}

function renderTabGuide() {
  const el = $('#tab-guide');
  if (!el) return;
  const main = state.view.split('/')[0];
  const key = main === 'host' ? 'host' : main === 'ap' ? 'ap' : main;
  el.innerHTML = guideHtml(key);
  el.classList.toggle('hidden', !state.showGuides || !TAB_GUIDES[key]);
}

function navTo(view, push = true) {
  if (radarAnim) { cancelAnimationFrame(radarAnim); radarAnim = null; }
  state.view = view;
  if (push) {
    if (state.stack[state.stack.length - 1] !== view) state.stack.push(view);
  } else {
    state.stack = [view];
  }
  render();
  $$('.nav button').forEach(b => {
    b.classList.toggle('active', b.dataset.view === view.split('/')[0]);
  });
  if (view.startsWith('host/')) {
    loadHostDetail(view.split('/')[1], { forensics: false });
  } else if (view.startsWith('ap/')) {
    loadApDetail(decodeURIComponent(view.slice(3)));
  } else {
    renderData();
  }
}

async function loadApDetail(bssid) {
  const el = $('#ap-detail');
  if (el) el.innerHTML = '<p class="inline-status active">Loading AP forensics…</p>';
  try {
    state.selectedAp = await API.ap(bssid);
  } catch (e) {
    console.error(e);
    state.selectedAp = { found: false, bssid };
  }
  renderApDetail();
  renderTabGuide();
}

function hostFromCache(ip) {
  const list = state.profiles.profiles?.length ? state.profiles.profiles : (state.hosts.hosts || []);
  const row = list.find(h => h.ip === ip);
  if (row) return { found: true, ...row, services: row.services || [] };
  return { found: false, ip };
}

async function loadHostDetail(ip, opts = {}) {
  const { forensics = false } = opts;
  const el = $('#host-detail');
  if (forensics && el) el.innerHTML = '<p class="inline-status active">Loading profile from deck…</p>';
  try {
    if (forensics) {
      state.selectedHost = await API.host(ip);
      state.hostForensicsLoaded = true;
    } else {
      state.selectedHost = hostFromCache(ip);
      state.hostForensicsLoaded = false;
    }
  } catch (e) {
    console.error(e);
    state.selectedHost = { found: false, ip };
    state.hostForensicsLoaded = false;
  }
  renderHostDetail();
  renderTabGuide();
}

function goBack() {
  if (state.stack.length <= 1) return;
  if (radarAnim) { cancelAnimationFrame(radarAnim); radarAnim = null; }
  state.stack.pop();
  state.view = state.stack[state.stack.length - 1];
  render();
  $$('.nav button').forEach(b => {
    b.classList.toggle('active', b.dataset.view === state.view.split('/')[0]);
  });
  if (state.view.startsWith('host/')) {
    const ip = state.view.split('/')[1];
    if (state.hostForensicsLoaded) loadHostDetail(ip, { forensics: true });
    else loadHostDetail(ip, { forensics: false });
  } else if (state.view.startsWith('ap/')) {
    loadApDetail(decodeURIComponent(state.view.slice(3)));
  } else {
    renderData();
  }
}

let _refreshInFlight = false;

async function refresh() {
  if (_refreshInFlight) return;  // avoid request pile-up on a slow deck
  _refreshInFlight = true;
  try {
    // One round-trip instead of 7 parallel requests (the ESP32 async server has
    // a tiny connection pool; batching keeps the link stable).
    const all = await API.all();
    if (all.busy) {                       // deck deferred (low heap) — keep current data
      setLinkStatus(true);
      return;
    }
    const summary = all.summary || {};
    const wifi = all.wifi || {};
    const hosts = all.hosts || {};
    const profiles = all.profiles || {};
    const events = all.events || {};
    const health = all.health || {};
    const control = all.control || {};
    state.control = control;
    renderPortalBanner(health);
    state.summary = summary;
    state.wifi = wifi;
    state.hosts = hosts;
    state.profiles = profiles;
    state.events = events;
    if (state.view.startsWith('host/') && state.hostLiveRefresh && state.hostForensicsLoaded) {
      const ip = state.view.split('/')[1];
      if (!state.hostActionBusy) {
        try {
          state.selectedHost = await API.host(ip);
        } catch (e) { /* keep prior */ }
      }
    } else if (state.view.startsWith('host/')) {
      const ip = state.view.split('/')[1];
      const cached = hostFromCache(ip);
      if (cached.found && state.selectedHost?.ip === ip) {
        state.selectedHost = { ...state.selectedHost, ...cached };
      }
    }
    if (state.view.startsWith('ap/') && !state.apActionBusy) {
      const bssid = decodeURIComponent(state.view.slice(3));
      if (!state.selectedAp || state.selectedAp.ap?.bssid !== bssid) {
        state.selectedAp = await API.ap(bssid);
      }
    }
    setLinkStatus(true);
    renderData();
  } catch (e) {
    console.error(e);
    setLinkStatus(false, 'Intel link lost — retrying…');
  } finally {
    _refreshInFlight = false;
  }
}

function renderData() {
  const s = state.summary;
  const tick = $('#ticker');
  if (tick) {
    tick.textContent = `${s.phaseLabel || s.phase || '—'} │ ${s.hostCount || 0} hosts │ ${s.wifiCount || 0} APs │ ${s.openPorts || 0} ports │ ${s.ip || ''}`;
  }
  $$('[data-stat]').forEach(el => {
    const k = el.dataset.stat;
    if (s[k] !== undefined) el.textContent = s[k];
  });
  const pf = $('#scan-progress');
  if (pf) {
    const { pct } = progressBarMeta(s);
    pf.style.width = pct + '%';
    pf.title = s.progressLabel || s.phaseLabel || '';
  }

  renderScanStatus();
  renderOpsBar();
  renderTabGuide();

  if (state.view === 'home') renderDashboard();

  if (state.view === 'spectrum') renderSpectrum();
  if (state.view === 'netmap') renderNetmap();
  if (state.view === 'phantom') renderPhantom();
  if (state.view === 'profiles') renderProfiles();
  if (state.view.startsWith('host/')) renderHostDetail();
  if (state.view.startsWith('ap/')) renderApDetail();
}

function hostRows() {
  const p = state.profiles.profiles || [];
  const h = state.hosts.hosts || [];
  return p.length ? p : h;
}

function dashboardUrl() {
  const ip = state.summary?.ip;
  return ip ? `http://${ip}/` : window.location.href;
}

function setOrchStatus(msg, ok = true) {
  const el = $('#orch-status');
  if (!el) return;
  el.textContent = msg;
  el.className = 'orch-status ' + (ok ? 'ok' : 'err');
}

function renderOrchestration() {
  const s = state.summary;
  const meta = $('#orch-meta');
  if (meta) {
    const reconOn = !s.orchestrationPaused;
    const phase = s.phase || '—';
    meta.innerHTML = `<span class="orch-chip ${reconOn ? 'ok' : 'warn'}">Auto-recon: ${reconOn ? '\u25CF ON' : 'PAUSED'}</span>
      <span class="orch-chip phase">${esc(phase)}</span>
      <span class="orch-chip">${s.hostCount || 0} hosts · Q${s.profileQueue || 0}</span>`;
  }

  const grid = $('#orch-actions');
  if (grid && !grid.dataset.bound) {
    grid.dataset.bound = '1';
    grid.innerHTML = CONTROL_ACTIONS.map(a => `
      <button type="button" class="btn-orch${a.danger ? ' btn-orch-danger' : ''}" data-action="${a.action}" title="${esc(a.hint)}">
        <span class="btn-orch-title">${esc(a.label)}</span>
        <span class="btn-orch-hint">${esc(a.hint)}</span>
      </button>`).join('');
    grid.querySelectorAll('[data-action]').forEach(btn => {
      btn.onclick = async () => {
        if (orchestrating) return;
        const action = btn.dataset.action;
        if (action === 'clear_discovery' || action === 'clear_log') {
          if (!confirm('Run “' + btn.querySelector('.btn-orch-title').textContent + '” on the deck?')) return;
        }
        setOrchStatus('Sending ' + action + '…');
        try {
          const res = await API.control(action);
          setOrchStatus(res.message || (res.ok ? 'Done' : 'Failed'), res.ok);
          await refresh();
        } catch (e) {
          setOrchStatus(e.message, false);
        }
      };
    });
  }
}

function sleep(ms) {
  return new Promise(r => setTimeout(r, ms));
}

async function waitForDeckIdle({ timeoutMs = 600000 } = {}) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const all = await API.all();
    if (all.busy) {
      await sleep(2000);
      continue;
    }
    const s = all.summary || {};
    state.summary = s;
    const busy = s.phase === 'LAN_SWEEP' || s.sweepActive;
    const profiling = s.phase === 'PROFILE' || (s.profileQueue || 0) > 0;
    if (!busy && !profiling) return s;
    setOrchStatus(
      `${s.phaseLabel || s.phase} · ${s.progress || 0}% · queue ${s.profileQueue || 0}`,
      true
    );
    await sleep(2000);
  }
  throw new Error('Timed out waiting for deck');
}

async function runPlaybook(steps, label) {
  if (orchestrating) {
    setOrchStatus('Playbook already running', false);
    return;
  }
  orchestrating = true;
  setOrchStatus(label + '…');
  try {
    for (const step of steps) {
      setOrchStatus(step.status);
      if (step.action) {
        const res = await API.control(step.action, step.ip || '');
        if (!res.ok) throw new Error(res.message || step.action + ' failed');
      }
      if (step.wait) await waitForDeckIdle({ timeoutMs: step.timeoutMs || 600000 });
      if (step.refresh) await refresh();
    }
    setOrchStatus(label + ' complete');
    await refresh();
  } catch (e) {
    setOrchStatus(e.message, false);
  } finally {
    orchestrating = false;
  }
}

function bindOrchestration() {
  const recon = $('#btn-playbook-recon');
  if (recon && !recon.dataset.bound) {
    recon.dataset.bound = '1';
    recon.onclick = () => runPlaybook([
      { status: 'Pausing auto scans…', action: 'pause' },
      { status: 'LAN sweep in progress…', action: 'lan_sweep' },
      { wait: true },
      { status: 'RF scan…', action: 'rf_scan' },
      { status: 'Profiling all hosts…', action: 'profile_all' },
      { wait: true },
      { status: 'Writing export…', action: 'export' },
      { status: 'Resuming auto scans…', action: 'resume' },
    ], 'Full recon');
  }
  const se = $('#btn-playbook-scan-export');
  if (se && !se.dataset.bound) {
    se.dataset.bound = '1';
    se.onclick = () => runPlaybook([
      { status: 'LAN sweep…', action: 'lan_sweep' },
      { wait: true },
      { status: 'Exporting…', action: 'export' },
    ], 'Scan + export');
  }
  const pe = $('#btn-playbook-profile-export');
  if (pe && !pe.dataset.bound) {
    pe.dataset.bound = '1';
    pe.onclick = () => runPlaybook([
      { status: 'Profile all hosts…', action: 'profile_all' },
      { wait: true },
      { status: 'Exporting…', action: 'export' },
    ], 'Profile + export');
  }
}

async function profileHostFromWeb(ip, ev) {
  if (ev) ev.stopPropagation();
  setOrchStatus('Queue profile for ' + ip + '…');
  try {
    const res = await API.control('profile_host', ip);
    setOrchStatus(res.message || 'Queued', res.ok);
    await refresh();
  } catch (e) {
    setOrchStatus(e.message, false);
  }
}

function renderAppLauncher() {
  const el = $('#app-launcher');
  if (!el) return;
  el.innerHTML = APP_TILES.map(t => `
    <button type="button" class="app-tile" data-view="${t.view}">
      <span class="app-tile-icon">${t.icon}</span>
      <span class="app-tile-title">${esc(t.title)}</span>
      <span class="app-tile-desc">${esc(t.desc)}</span>
    </button>`).join('');
  el.querySelectorAll('.app-tile').forEach(btn => {
    btn.onclick = () => navTo(btn.dataset.view, false);
  });
}

function setShareStatus(msg, ok = true) {
  const el = $('#share-status');
  if (!el) return;
  el.textContent = msg;
  el.className = 'share-status ' + (ok ? 'ok' : 'err');
}

async function fetchExportBundle(refresh = false) {
  try {
    return await API.exportBundle(refresh);
  } catch {
    return {
      schema: 'cyd2-intel-export-v1',
      exportedAt: new Date().toISOString(),
      dashboard: dashboardUrl(),
      summary: state.summary,
      wifi: state.wifi,
      hosts: state.hosts,
      profiles: state.profiles,
      events: state.events,
    };
  }
}

async function copyText(text) {
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const ta = document.createElement('textarea');
  ta.value = text;
  document.body.appendChild(ta);
  ta.select();
  document.execCommand('copy');
  ta.remove();
}

function bindShareActions() {
  const map = {
    'btn-copy-url': async () => {
      await copyText(dashboardUrl());
      setShareStatus('Dashboard link copied');
    },
    'btn-copy-json': async () => {
      setShareStatus('Building JSON…', true);
      const b = await fetchExportBundle(false);
      await copyText(JSON.stringify(b, null, 2));
      setShareStatus('JSON copied to clipboard');
    },
    'btn-download-json': async () => {
      setShareStatus('Preparing download…', true);
      const b = await fetchExportBundle(false);
      const blob = new Blob([JSON.stringify(b, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `cyberdeck-intel-${state.summary?.ip || 'export'}.json`;
      a.click();
      URL.revokeObjectURL(a.href);
      setShareStatus('JSON file downloaded');
    },
    'btn-download-report': async () => {
      setShareStatus('Preparing report…', true);
      let text;
      try {
        text = await API.reportText(false);
      } catch {
        text = await buildClientReport();
      }
      const blob = new Blob([text], { type: 'text/plain' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `cyberdeck-report-${state.summary?.ip || 'lan'}.txt`;
      a.click();
      URL.revokeObjectURL(a.href);
      setShareStatus('Report downloaded');
    },
    'btn-web-share': async () => {
      if (!navigator.share) {
        setShareStatus('Web Share not supported — use Copy JSON', false);
        return;
      }
      const b = await fetchExportBundle(false);
      const text = await buildClientReport();
      await navigator.share({
        title: 'CYBERDECK Net Intel',
        text: text.slice(0, 8000),
        url: dashboardUrl(),
      });
      setShareStatus('Shared via system dialog');
    },
    'btn-refresh-export': async () => {
      setShareStatus('Refreshing snapshot on deck…', true);
      await API.exportBundle(true);
      await API.reportText(true);
      await refresh();
      setShareStatus('Deck snapshot updated — ready to download');
    },
  };
  Object.entries(map).forEach(([id, fn]) => {
    const btn = $('#' + id);
    if (btn) btn.onclick = () => fn().catch(e => setShareStatus(e.message, false));
  });
}

async function buildClientReport() {
  const s = state.summary;
  const hosts = hostRows();
  const aps = state.wifi.aps || [];
  let out = 'CYBERDECK NET INTEL REPORT\n==========================\n\n';
  out += `Network: ${s.ssid || '—'}  IP: ${s.ip || '—'}\n`;
  out += `Hosts: ${s.hostCount || 0}  APs: ${s.wifiCount || 0}  Phase: ${s.phase || '—'}\n\n`;
  out += '--- LAN HOSTS ---\n';
  hosts.forEach(h => {
    out += `${h.ip}  risk=${h.risk ?? 0}  ports=${h.ports ?? 0}  ${h.vendor || ''} ${h.deviceType || ''}\n`;
  });
  out += '\n--- NEARBY WI-FI ---\n';
  aps.forEach(a => {
    out += `${a.ssid}  ${a.rssi} dBm  ch${a.channel}  ${a.enc}\n`;
  });
  out += '\n--- LOG ---\n';
  (state.events.events || []).forEach(e => { out += e + '\n'; });
  return out;
}

function renderDashboard() {
  const s = state.summary;
  const hosts = hostRows();
  const aps = state.wifi.aps || [];

  renderAppLauncher();
  renderOrchestration();

  const urlEl = $('#dash-url');
  if (urlEl && s.ip) {
    urlEl.href = 'http://' + s.ip + '/';
    urlEl.textContent = 'http://' + s.ip + '/';
  }

  const alertsEl = $('#dashboard-alerts');
  if (alertsEl) {
    const items = [];
    aps.filter(a => a.encType === 0).forEach(a => {
      items.push(`<div class="alert-item risk-high">Open Wi-Fi: ${esc(a.ssid)} (${a.rssi} dBm)</div>`);
    });
    hosts.filter(h => (h.risk || 0) >= 70).forEach(h => {
      items.push(`<div class="alert-item risk-high clickable" data-ip="${h.ip}">High risk host ${h.ip} — tap for detail</div>`);
    });
    hosts.filter(h => (h.cveCount || 0) > 0).slice(0, 3).forEach(h => {
      items.push(`<div class="alert-item risk-med clickable" data-ip="${h.ip}">${h.cveCount} CVE match(es): ${h.ip}</div>`);
    });
    if (!items.length) {
      items.push('<div class="alert-item ok">No critical alerts — sweep continues in background</div>');
    }
    alertsEl.innerHTML = '<div class="panel" data-label="ALERTS">' + items.join('') + '</div>';
  }

  const hb = $('#dashboard-hosts-body');
  if (hb) {
    hb.innerHTML = hosts.length
      ? hosts.map(h => `<tr class="clickable" data-ip="${h.ip}" title="Tap for ports & CVEs">
          <td><strong>${h.ip}</strong></td>
          <td>${esc(h.vendor || '—')}</td>
          <td>${esc(h.deviceType || '—')}</td>
          <td>${h.ports ?? (h.open ? h.open.length : 0)}</td>
          <td class="${riskClass(h.risk)}">${h.risk ?? 0}</td>
          <td><button type="button" class="btn-row-action" data-profile-ip="${h.ip}" title="Queue profiling">Profile</button></td></tr>`).join('')
      : `<tr><td colspan="6" class="empty-row">No hosts yet — ${esc(s.phaseLabel || s.phase || 'scanning')} (${s.progress || 0}%)</td></tr>`;
    hb.querySelectorAll('[data-profile-ip]').forEach(btn => {
      btn.onclick = e => profileHostFromWeb(btn.dataset.profileIp, e);
    });
  }

  const wb = $('#dashboard-wifi-body');
  if (wb) {
    wb.innerHTML = aps.length
      ? aps.slice(0, 16).map(a => `<tr class="clickable" data-bssid="${escAttr(a.bssid)}" title="Tap for AP tools">
          <td>${esc(a.ssid)}</td>
          <td>${esc(a.vendor || '—')}</td>
          <td>${a.channel}</td>
          <td class="${a.encType === 0 ? 'risk-high' : ''}">${esc(a.enc)}</td>
          <td>${a.rssi}</td></tr>`).join('')
      : `<tr><td colspan="5" class="empty-row">No APs yet — RF scan runs when not sweeping LAN</td></tr>`;
  }

  renderEvents();
}

function renderBreadcrumb() {
  const bc = $('#breadcrumb');
  if (!bc) return;
  const labels = { home: 'HOME', spectrum: 'WI-FI', netmap: 'LAN MAP', phantom: 'THREATS', profiles: 'DEVICES' };
  const parts = state.stack.map((v, i) => {
    const main = v.split('/')[0];
    let label = labels[main] || v.toUpperCase();
    if (v.startsWith('host/')) label = 'HOST › ' + v.split('/')[1];
    if (v.startsWith('ap/')) {
      const b = decodeURIComponent(v.slice(3));
      const ap = (state.wifi.aps || []).find(a => a.bssid === b);
      label = 'AP › ' + (ap?.ssid || b);
    }
    if (i === state.stack.length - 1) return `<span>${esc(label)}</span>`;
    return `<a data-back="${i}">${esc(label)}</a> / `;
  });
  bc.innerHTML = parts.join('');
  bc.querySelectorAll('[data-back]').forEach(a => {
    a.onclick = () => {
      const idx = +a.dataset.back;
      state.stack = state.stack.slice(0, idx + 1);
      state.view = state.stack[state.stack.length - 1];
      render();
      renderData();
      if (state.view.startsWith('host/')) {
        const ip = state.view.split('/')[1];
        if (state.hostForensicsLoaded) loadHostDetail(ip, { forensics: true });
        else loadHostDetail(ip, { forensics: false });
      }
      if (state.view.startsWith('ap/')) loadApDetail(decodeURIComponent(state.view.slice(3)));
    };
  });
}

function renderSpectrum() {
  const el = $('#spectrum-body');
  if (!el) return;
  const aps = state.wifi.aps || [];
  const s = state.summary;
  const channels = {};
  aps.forEach(a => {
    const c = a.channel || 0;
    channels[c] = (channels[c] || 0) + 1;
  });
  const chKeys = Object.keys(channels).sort((a, b) => a - b);
  let bars = '<div class="bar-chart">';
  chKeys.forEach(c => {
    const h = Math.min(100, channels[c] * 25);
    bars += `<div class="bar" style="height:${h}%"><span>${c}</span></div>`;
  });
  bars += '</div>';

  const openCount = aps.filter(a => a.encType === 0).length;
  const hiddenCount = aps.filter(a => a.hidden).length;

  let rows = aps.map(a => `<tr class="clickable" data-bssid="${escAttr(a.bssid)}" title="Tap for AP tools">
    <td>${esc(a.ssid)}</td><td class="mono">${esc(a.bssid)}</td><td>${a.rssi}</td><td>${a.channel}</td>
    <td>${a.enc}</td><td class="${riskClass(a.risk)}">${a.risk}</td></tr>`).join('');

  const rfNote = s.phase === 'RF_SCAN'
    ? '<p class="inline-status active">● RF scan in progress — table refreshes when complete</p>'
    : (aps.length
      ? `<p class="inline-status">Last RF update ~${s.rfAgeSec ?? '?'}s ago · ${aps.length} APs · ${openCount} open · ${hiddenCount} hidden</p>`
      : '<p class="inline-status warn">No AP data yet — first RF scan runs shortly after connect</p>');

  el.innerHTML = `
    ${rfNote}
    <div class="panel" data-label="CHANNEL MAP">
      <h3 class="panel-title">CHANNEL MAP</h3>
      <p class="panel-desc">Bars show AP count per Wi-Fi channel — tall clusters mean congestion on that channel.</p>
      ${bars || '<p class="empty">No channel data</p>'}
    </div>
    <div class="panel" data-label="ACCESS POINTS">
      <h3 class="panel-title">ACCESS POINTS</h3>
      <p class="panel-desc">Tap any row for AP forensics and tools (security audit, signal profile, channel audit, etc.).</p>
      <table><thead><tr><th>SSID</th><th>BSSID</th><th>RSSI</th><th>CH</th><th>ENC</th><th>RISK</th></tr></thead>
      <tbody>${rows || '<tr><td colspan="6">Waiting for RF scan…</td></tr>'}</tbody></table>
    </div>`;
}

let radarAnim = null;
let _netmapListKey = '';

function renderNetmapStatic() {
  const canvas = $('#radar');
  if (!canvas) return;
  const s = state.summary;
  const hosts = state.hosts.hosts || [];
  const lanNote = s.phase === 'LAN_SWEEP'
    ? `Sweeping ${s.scanTarget || 'subnet'} — ${s.lanChecked ?? s.lanIndex ?? 0}/${s.lanTotal || 253} checked · ${s.hostCount || 0} hosts found`
    : `${hosts.length} hosts cached · ${s.sweepCount || 0} sweeps · hit rate ${(s.hitRatePct ?? s.discoveryPct) ?? 0}%`;

  let statusEl = $('#netmap-status');
  if (!statusEl) {
    statusEl = document.createElement('p');
    statusEl.id = 'netmap-status';
    statusEl.className = 'inline-status';
    canvas.before(statusEl);
  }
  statusEl.textContent = lanNote;
  statusEl.classList.toggle('active', s.phase === 'LAN_SWEEP');

  const list = $('#host-list');
  if (!list) return;
  const key = hosts.map(h => `${h.ip}:${h.risk}:${h.ports}`).join('|') + '|' + lanNote;
  if (key === _netmapListKey) return;
  _netmapListKey = key;
  list.innerHTML = hosts.map(h =>
    `<tr class="clickable" data-ip="${h.ip}"><td>${esc(h.name || '—')}</td><td>${h.ip}</td>
     <td>${esc(h.discoveredBy || '—')}</td><td>${h.latency}ms</td>
     <td>${h.ports}</td><td class="${riskClass(h.risk)}">${h.risk}</td></tr>`
  ).join('') || '<tr><td colspan="6">No hosts yet — passive mDNS/SSDP or LAN sweep in progress</td></tr>';
}

function renderNetmapRadarFrame() {
  const canvas = $('#radar');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width = canvas.offsetWidth * 2 || 600;
  const h = canvas.height = 640;
  const cx = w / 2, cy = h / 2, R = Math.min(cx, cy) - 20;
  const t = Date.now() / 1000;

  ctx.fillStyle = '#020408';
  ctx.fillRect(0, 0, w, h);

  for (let r = 1; r <= 4; r++) {
    ctx.strokeStyle = 'rgba(0,255,249,.15)';
    ctx.beginPath();
    ctx.arc(cx, cy, (R * r) / 4, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.strokeStyle = 'rgba(0,255,249,.4)';
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(t) * R, cy + Math.sin(t) * R);
  ctx.stroke();

  const hosts = state.hosts.hosts || [];
  hosts.forEach((host, i) => {
    const parts = host.ip.split('.').map(Number);
    const angle = ((parts[3] || i) / 255) * Math.PI * 2 + t * 0.2;
    const dist = R * (0.25 + (host.latency || 50) / 200);
    const x = cx + Math.cos(angle) * dist;
    const y = cy + Math.sin(angle) * dist;
    const col = host.risk >= 70 ? '#ff2a4a' : host.risk >= 40 ? '#ffb000' : '#39ff14';
    ctx.fillStyle = col;
    ctx.shadowColor = col;
    ctx.shadowBlur = 12;
    ctx.beginPath();
    ctx.arc(x, y, 8, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
    ctx.fillStyle = '#c8e8ff';
    ctx.font = '20px Share Tech Mono';
    ctx.fillText(String(parts[3]), x + 10, y + 4);
  });
}

function renderNetmap() {
  if (state.view !== 'netmap') {
    if (radarAnim) { cancelAnimationFrame(radarAnim); radarAnim = null; }
    _netmapListKey = '';
    return;
  }
  renderNetmapStatic();
  renderNetmapRadarFrame();
  radarAnim = requestAnimationFrame(renderNetmapLoop);
}

function renderNetmapLoop() {
  if (state.view !== 'netmap') {
    radarAnim = null;
    return;
  }
  renderNetmapRadarFrame();
  radarAnim = requestAnimationFrame(renderNetmapLoop);
}

function renderProfiles() {
  const el = $('#profiles-body');
  if (!el) return;
  const list = state.profiles.profiles || state.hosts.hosts || [];
  const s = state.summary;
  const status = s.phase === 'PROFILE'
    ? `Profiling queue: ${s.profileQueue || 0} · rules loaded: ${state.profiles.vulnRules || 0}`
    : `${state.profiles.profiled || 0}/${list.length} profiled · ${state.profiles.totalCves || s.totalCves || 0} CVE matches`;

  let rows = list.map(p => `<tr class="clickable" data-ip="${p.ip}">
    <td>${p.ip}</td>
    <td class="mono">${esc(p.mac || '—')}</td>
    <td>${esc(p.vendor || '—')}</td>
    <td>${esc(p.deviceType || '—')}</td>
    <td>${esc(p.osGuess || '—')}</td>
    <td>${p.profileConf != null ? p.profileConf + '%' : '—'}</td>
    <td class="${riskClass(p.cveCount * 15)}">${p.cveCount ?? 0}</td>
    <td class="${riskClass(p.risk)}">${p.risk}</td>
  </tr>`).join('');

  el.innerHTML = `
    <p class="inline-status ${s.phase === 'PROFILE' ? 'active' : ''}">${esc(status)}</p>
    <div class="panel" data-label="DEVICE FLEET">
      <p class="panel-desc">Tap a row for host tools (forensics, CVE report, CIRCL). Profiling does <strong>not</strong> start until you run it — unless auto-profile is on in the deck bar above.</p>
      <table class="profile-table">
        <thead><tr>
          <th>IP</th><th>MAC</th><th>VENDOR</th><th>TYPE</th><th>OS</th><th>CONF</th><th>CVEs</th><th>RISK</th>
        </tr></thead>
        <tbody>${rows || '<tr><td colspan="8">No hosts discovered yet — run a LAN sweep from NETMAP</td></tr>'}</tbody>
      </table>
    </div>`;

}

function cveIdLabel(id) {
  if (!id) return '—';
  if (id.startsWith('EXPOSURE-')) return id + ' (rule)';
  return id;
}

function cveRows(cves) {
  if (!cves || !cves.length) return '<p class="empty">No CVE matches for this profile</p>';
  return `<table class="cve-table"><thead><tr><th>ID</th><th>SEV</th><th>CVSS</th><th>SUMMARY</th></tr></thead><tbody>` +
    cves.map(c => `<tr>
      <td class="mono">${esc(cveIdLabel(c.id))}</td>
      <td class="sev-${(c.severity || '').toLowerCase()}">${esc(c.severity)}</td>
      <td>${c.cvss != null ? c.cvss : '—'}</td>
      <td>${esc(c.summary)}</td>
    </tr>`).join('') + '</tbody></table>';
}

async function searchCircl(btn) {
  const q = decodeURIComponent(btn.dataset.q || '');
  const box = $('#circl-results');
  if (!box) return;
  box.innerHTML = '<p class="inline-status active">Querying CIRCL CVE API…</p>';
  try {
    const data = await fetch('https://cve.circl.lu/api/search/' + encodeURIComponent(q))
      .then(r => { if (!r.ok) throw new Error('CIRCL HTTP ' + r.status); return r.json(); });
    const items = Array.isArray(data) ? data : (data.results || []);
    if (!items.length) {
      box.innerHTML = '<p class="empty">No additional CVEs returned for this query</p>';
      return;
    }
    const rows = items.slice(0, 15).map(item => {
      const id = item.id || item.cve || item;
      const summary = (typeof item === 'object' && item.summary) ? item.summary : String(item).slice(0, 120);
      return `<tr><td class="mono">${esc(id)}</td><td colspan="3">${esc(summary)}</td></tr>`;
    }).join('');
    box.innerHTML = `<p class="panel-desc">Live results from <a href="https://cve.circl.lu/" target="_blank" rel="noopener">cve.circl.lu</a> (public MITRE/NVD mirror)</p>
      <table class="cve-table"><thead><tr><th>CVE</th><th colspan="3">DETAIL</th></tr></thead><tbody>${rows}</tbody></table>`;
  } catch (e) {
    box.innerHTML = `<p class="warn">CIRCL search failed: ${esc(e.message)}</p>`;
  }
}

function renderPhantom() {
  const el = $('#phantom-body');
  if (!el) return;
  const hosts = [...(state.hosts.hosts || [])].sort((a, b) => b.risk - a.risk);
  const openWifi = (state.wifi.aps || []).filter(a => a.encType === 0);
  const s = state.summary;

  let cards = hosts.slice(0, 12).map(h => `
    <div class="demo-card" style="cursor:pointer" data-ip="${h.ip}">
      <h2>${h.ip}</h2>
      <p>Exposure score <span class="${riskClass(h.risk)}">${h.risk}/100</span> — ${h.ports} open services</p>
      <span class="tag">DRILL IN →</span>
    </div>`).join('');

  let wifiWarn = openWifi.map(a =>
    `<tr><td>${esc(a.ssid)}</td><td class="risk-high">OPEN</td><td>${a.rssi} dBm</td></tr>`
  ).join('');

  el.innerHTML = `
    <p class="inline-status">Surface analysis from ${s.hostCount || 0} hosts, ${s.wifiCount || 0} APs, ${s.openPorts || 0} open ports logged</p>
    <div class="stats">
      <div class="stat"><div class="val risk-high">${s.maxRisk || 0}</div><div class="lbl">MAX RISK</div></div>
      <div class="stat"><div class="val">${openWifi.length}</div><div class="lbl">OPEN APs</div></div>
      <div class="stat"><div class="val">${s.criticalHosts ?? hosts.filter(h => h.risk >= 70).length}</div><div class="lbl">CRITICAL HOSTS</div></div>
      <div class="stat"><div class="val">${(s.hitRatePct ?? s.discoveryPct) ?? 0}%</div><div class="lbl">HOST HIT RATE</div></div>
    </div>
    <div class="panel" data-label="NEARBY THREATS">
      <p class="panel-desc">Open Wi-Fi networks visible to RF scan — potential evil-twin or piggyback risk.</p>
      ${wifiWarn ? '<table><thead><tr><th>SSID</th><th>ENC</th><th>RSSI</th></tr></thead><tbody>' + wifiWarn + '</tbody></table>'
        : '<p style="color:var(--green)">No open APs in RF range</p>'}
    </div>
    <div class="panel" data-label="PRIORITIZED HOSTS">
      <p class="panel-desc">LAN endpoints ranked by exposure score. Tap to inspect ports and service severity.</p>
      <div class="demos">${cards || '<p class="empty">No hosts discovered yet — wait for LAN sweep</p>'}</div>
    </div>`;

}

function findingRows(findings) {
  if (!findings?.length) return '';
  return findings.map(f => {
    const sev = (f.severity || 'info').toLowerCase();
    return `<div class="finding-row sev-${esc(sev)}">
      <div class="finding-title">${esc(f.title)}</div>
      <div class="panel-desc">${esc(f.detail)}</div></div>`;
  }).join('');
}

function renderApActionResult(r) {
  const el = $('#ap-action-results');
  if (!el) return;
  let extra = '';
  if (r.rssiAvg != null) {
    extra += `<p>RSSI samples: min ${r.rssiMin} / avg ${r.rssiAvg} / max ${r.rssiMax} dBm (${r.rssiSamples} hits)</p>`;
  }
  const co = r.coChannelAps || [];
  if (co.length) {
    extra += '<h4 class="section-title">Co-channel APs</h4><ul>' +
      co.map(a => `<li>${esc(a.ssid)} ${esc(a.bssid)} ch${a.channel ?? '—'} ${a.rssi} dBm</li>`).join('') + '</ul>';
  }
  const twins = r.ssidMatches || [];
  if (twins.length) {
    extra += '<h4 class="section-title">SSID collisions</h4><ul>' +
      twins.map(a => `<li>${esc(a.bssid)} ch${a.channel} ${a.rssi} dBm</li>`).join('') + '</ul>';
  }
  el.innerHTML = `
    <h3 class="section-title">Scenario: ${esc(r.action)}</h3>
    <p class="${r.ok ? 'inline-status' : 'inline-status warn'}">${esc(r.message || (r.ok ? 'Done' : 'Failed'))}</p>
    ${findingRows(r.findings)}
    ${extra}`;
}

async function runApScenario(action) {
  const bssid = state.selectedAp?.ap?.bssid || decodeURIComponent(state.view.slice(3));
  const status = $('#ap-action-status');
  const results = $('#ap-action-results');
  if (!bssid) return;
  state.apActionBusy = true;
  if (status) {
    status.classList.remove('hidden');
    status.textContent = 'Running ' + action + '…';
    status.className = 'orch-status';
  }
  if (results) results.innerHTML = '';
  try {
    const r = await API.apAction(bssid, action);
    if (status) {
      status.textContent = r.message || (r.ok ? 'Complete' : 'Failed');
      status.className = 'orch-status ' + (r.ok ? 'ok' : 'err');
    }
    renderApActionResult(r);
    if (action === 'rescan') setTimeout(() => refresh(), 8000);
  } catch (e) {
    console.error(e);
    if (status) {
      status.textContent = 'Action failed — ' + e.message;
      status.className = 'orch-status err';
    }
  } finally {
    state.apActionBusy = false;
  }
}

function renderApDetail() {
  const guideEl = $('#ap-guide');
  if (guideEl) guideEl.innerHTML = guideHtml('ap');

  const el = $('#ap-detail');
  const scen = $('#ap-scenarios');
  if (!el) return;
  const d = state.selectedAp;
  if (!d?.found) {
    if (scen) scen.classList.add('hidden');
    el.innerHTML = `<p class="warn">AP not in last RF scan.</p>
      <p class="panel-desc">Run an RF scan from HOME, then tap the AP again. Each BSSID is tracked separately (duplicate SSIDs show as distinct rows).</p>`;
    return;
  }
  const a = d.ap || {};
  const conn = d.isConnectedAp
    ? '<span class="inline-status">● You are associated to this BSSID</span>'
    : (d.connectedBssid ? `<span class="dim">Connected: ${esc(d.connectedSsid)} ${esc(d.connectedBssid)}</span>` : '');
  el.innerHTML = `
    <h2 style="font-family:Orbitron;color:var(--cyan);margin-bottom:8px">${esc(a.ssid)}</h2>
    <p class="mono">${esc(a.bssid)} · ${esc(a.vendor || '—')} · ${d.band || '—'} · Ch ${a.channel}</p>
    <p>RSSI ${a.rssi} dBm · ${esc(a.enc)} · Risk <span class="${riskClass(d.risk ?? a.risk)}">${d.risk ?? a.risk}/100</span>
      ${a.hidden ? ' · <span class="warn">Hidden SSID</span>' : ''}</p>
    ${conn}
    <p class="panel-desc">${d.coChannelCount || 0} other AP(s) on this channel in last scan.</p>`;

  const toolsLbl = $('#ap-tools-label');
  if (toolsLbl) toolsLbl.classList.remove('hidden');

  if (scen) {
    scen.classList.remove('hidden');
    scen.innerHTML = AP_SCENARIOS.map(s =>
      `<button type="button" class="btn-row-action" data-ap-action="${s.action}" title="${esc(s.hint)}">
        <span class="lbl">${esc(s.label)}</span><span class="hint">${esc(s.hint)}</span></button>`
    ).join('') +
      `<button type="button" class="btn-row-action" data-ap-action="rescan" title="Refresh global AP table">
        <span class="lbl">RF rescan</span><span class="hint">Full spectrum refresh</span></button>`;
    scen.querySelectorAll('[data-ap-action]').forEach(btn => {
      btn.onclick = () => runApScenario(btn.dataset.apAction);
    });
  }
}

async function runHostAction(actionId, ip) {
  const status = $('#host-action-status');
  state.hostActionBusy = true;
  if (status) {
    status.classList.remove('hidden');
    status.className = 'orch-status';
    status.textContent = 'Working…';
  }

  try {
    if (actionId === 'forensics') {
      if (status) status.textContent = 'Queuing forensics on deck…';
      const res = await API.control('profile_host', ip);
      if (!res.ok && res.message) throw new Error(res.message);
      if (status) status.textContent = 'Profiling on deck — waiting…';
      for (let i = 0; i < 24; i++) {
        await new Promise(r => setTimeout(r, 1500));
        const all = await API.all();
        if (!all.busy) {
          const s = all.summary || {};
          state.summary = s;
          if (s.phase !== 'PROFILE' && (s.profileQueue || 0) === 0) break;
        }
      }
      await loadHostDetail(ip, { forensics: true });
      if (status) status.className = 'orch-status ok';
      if (status) status.textContent = 'Forensics loaded';
      return;
    }
    if (actionId === 'load') {
      await loadHostDetail(ip, { forensics: true });
      if (status) { status.className = 'orch-status ok'; status.textContent = 'Report loaded'; }
      return;
    }
    if (actionId === 'circl') {
      if (!state.hostForensicsLoaded) await loadHostDetail(ip, { forensics: true });
      const btn = $('#btn-circl');
      if (btn) searchCircl(btn);
      if (status) { status.className = 'orch-status ok'; status.textContent = 'CVE search started'; }
      return;
    }
    if (actionId === 'lanmap') {
      navTo('netmap');
      if (status) status.classList.add('hidden');
      return;
    }
  } catch (e) {
    console.error(e);
    if (status) { status.className = 'orch-status err'; status.textContent = e.message; }
  } finally {
    state.hostActionBusy = false;
  }
}

function renderHostScenarios(ip) {
  const scen = $('#host-scenarios');
  if (!scen) return;
  scen.innerHTML = HOST_ACTIONS.map(a =>
    `<button type="button" class="btn-row-action" data-host-action="${a.id}" title="${esc(a.hint)}">
      <span class="lbl">${esc(a.label)}</span><span class="hint">${esc(a.hint)}</span></button>`
  ).join('') +
    `<button type="button" class="btn-row-action" data-host-live="${state.hostLiveRefresh ? '0' : '1'}">
      <span class="lbl">${state.hostLiveRefresh ? 'Pause live refresh' : 'Live refresh'}</span>
      <span class="hint">Auto-reload CVE block every 2.5s</span></button>`;
  scen.querySelectorAll('[data-host-action]').forEach(btn => {
    btn.onclick = () => runHostAction(btn.dataset.hostAction, ip);
  });
  const liveBtn = scen.querySelector('[data-host-live]');
  if (liveBtn) {
    liveBtn.onclick = () => {
      state.hostLiveRefresh = liveBtn.dataset.hostLive === '1';
      renderHostScenarios(ip);
      const st = $('#host-action-status');
      if (st) {
        st.classList.remove('hidden');
        st.className = 'orch-status ok';
        st.textContent = state.hostLiveRefresh ? 'Live refresh on' : 'Live refresh off';
      }
    };
  }
}

function renderHostDetail() {
  const guideEl = $('#host-guide');
  if (guideEl) guideEl.innerHTML = guideHtml('host');

  const el = $('#host-detail');
  const forensicsEl = $('#host-forensics');
  if (!el || !state.selectedHost) return;
  const h = state.selectedHost;
  const ip = h.ip || state.view.split('/')[1];

  if (!h.found) {
    if (forensicsEl) forensicsEl.classList.add('hidden');
    $('#host-scenarios')?.replaceChildren();
    el.innerHTML = `
      <p class="warn">Host not in cache yet.</p>
      <p class="panel-desc">Run a LAN sweep from HOME or wait for passive mDNS/SSDP discovery.</p>`;
    return;
  }

  const profiled = h.profiled || (h.profileConf != null && h.profileConf > 0) || (h.cveCount > 0);
  el.innerHTML = `
    <h2 style="font-family:Orbitron;color:var(--cyan);margin-bottom:8px">${esc(h.ip)}</h2>
    <p>${h.name ? esc(h.name) + ' · ' : ''}${h.latency != null ? `Latency ${h.latency}ms · ` : ''}Via ${esc(h.discoveredBy || '—')} ·
      Risk <span class="${riskClass(h.risk)}">${h.risk ?? 0}/100</span></p>
    <p class="panel-desc">Forensics run <strong>only when you choose</strong> below (unless auto-profile is enabled on the deck control bar).
      ${profiled ? ' This host has been profiled before.' : ' Not profiled yet.'}</p>`;

  renderHostScenarios(ip);

  if (!state.hostForensicsLoaded) {
    if (forensicsEl) {
      forensicsEl.classList.add('hidden');
      forensicsEl.innerHTML = '';
    }
    return;
  }

  const cves = h.cves || [];
  const searchQ = [h.vendor, h.deviceType, h.osGuess, h.banner].filter(Boolean).join(' ').trim() || h.ip;
  const svcs = (h.services || []).map(s =>
    `<div class="port-chip ${s.severity === 'CRITICAL' ? 'crit' : s.severity === 'WARN' ? 'warn' : ''}">
      <div>${s.port}</div><div style="font-size:.65rem">${s.name}</div>
      <div class="sev">${s.severity}</div></div>`
  ).join('');

  if (forensicsEl) {
    forensicsEl.classList.remove('hidden');
    forensicsEl.innerHTML = `
      <div class="profile-card">
        <div><span class="lbl">MAC</span> <span class="mono">${esc(h.mac || '—')}</span></div>
        <div><span class="lbl">Vendor</span> ${esc(h.vendor || '—')}</div>
        <div><span class="lbl">Type</span> ${esc(h.deviceType || '—')}</div>
        <div><span class="lbl">OS guess</span> ${esc(h.osGuess || '—')}</div>
        <div><span class="lbl">Banner</span> <span class="mono">${esc(h.banner || '—')}</span></div>
        <div><span class="lbl">Profile confidence</span> ${h.profileConf != null ? h.profileConf + '%' : '—'}</div>
      </div>
      <h3 class="section-title">KNOWN CVEs (${cves.length})</h3>
      ${cveRows(cves)}
      <button type="button" class="btn-ghost on" data-q="${encodeURIComponent(searchQ)}" id="btn-circl">Search CIRCL live</button>
      <div id="circl-results"></div>
      <h3 class="section-title">OPEN SERVICES</h3>
      <div class="port-grid">${svcs || '<p>No open ports logged</p>'}</div>`;
  }
  const btn = $('#btn-circl');
  if (btn) btn.onclick = () => searchCircl(btn);
}

function esc(s) {
  const d = document.createElement('div');
  d.textContent = s == null ? '' : String(s);
  return d.innerHTML;
}

function escAttr(s) {
  return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;');
}

function render() {
  const bc = $('#breadcrumb');
  if (bc) bc.classList.toggle('hidden', state.view === 'home' && state.stack.length <= 1);
  renderBreadcrumb();
  renderTabGuide();
  const backBtn = $('#nav-back');
  if (backBtn) {
    const canBack = state.stack.length > 1;
    backBtn.classList.toggle('hidden', !canBack);
    backBtn.onclick = () => goBack();
  }
  $$('.view').forEach(v => v.classList.add('hidden'));
  const main = state.view.split('/')[0];
  const el = $('#view-' + main);
  if (el) el.classList.remove('hidden');
}

function bindAppEvents() {
  const app = $('#app');
  if (!app || app._bound) return;
  app._bound = true;
  app.addEventListener('click', e => {
    if (e.target.closest('nav.nav') || e.target.closest('#toggle-guides')) return;
    if (e.target.closest('button') && !e.target.closest('.demo-card') &&
        !e.target.closest('.btn-row-action') && !e.target.closest('.orch-grid') &&
        !e.target.closest('.orch-playbooks') && !e.target.closest('.share-actions') &&
        !e.target.closest('.app-launcher') && !e.target.closest('.ap-scenarios') &&
        !e.target.closest('.host-scenarios') && !e.target.closest('#ops-bar')) return;
    const apRow = e.target.closest('tr[data-bssid]');
    if (apRow?.dataset.bssid) {
      e.preventDefault();
      navTo('ap/' + encodeURIComponent(apRow.dataset.bssid));
      return;
    }
    const hostHit = e.target.closest('[data-ip]');
    if (hostHit?.dataset.ip && !hostHit.dataset.profileIp && !hostHit.dataset.bssid) {
      e.preventDefault();
      navTo('host/' + hostHit.dataset.ip);
    }
  });
  $$('.nav button').forEach(b => {
    b.onclick = () => navTo(b.dataset.view, false);
  });
  const tg = $('#toggle-guides');
  if (tg) {
    const syncGuideBtn = () => {
      tg.classList.toggle('on', state.showGuides);
      tg.setAttribute('aria-pressed', state.showGuides ? 'true' : 'false');
      tg.textContent = state.showGuides ? 'HELP \u25CF ON' : 'HELP \u25CB OFF';
    };
    syncGuideBtn();
    tg.onclick = () => {
      state.showGuides = !state.showGuides;
      sessionStorage.setItem('cyd_guides', state.showGuides ? '1' : '0');
      syncGuideBtn();
      renderTabGuide();
      if (state.view.startsWith('host/')) renderHostDetail();
      if (state.view.startsWith('ap/')) renderApDetail();
      renderOpsBar();
    };
  }
}

function renderEvents() {
  const log = $('#event-log');
  if (!log) return;
  const ev = state.events.events || [];
  const meta = state.events.persisted
    ? `<div class="log-meta">Saved on device flash (${state.events.logBytes || 0} bytes) · <a href="/api/intel/log" target="_blank" rel="noopener">download full log</a></div>`
    : '<div class="log-meta">Log will persist to flash after first event</div>';
  log.innerHTML = meta + (ev.length
    ? ev.map(e => `<div>${esc(e)}</div>`).join('')
    : '<div>Waiting for scan events…</div>');
}

function bindNav() {
  bindAppEvents();
  bindShareActions();
  bindOrchestration();
}

// Poll only while the tab is visible; refresh immediately when it regains focus
// (saves the deck's CPU/radio when the phone screen is off or app is backgrounded).
setInterval(() => { if (!document.hidden) refresh(); }, 3500);
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
refresh();
bindNav();
navTo('home', false);
state.stack = ['home'];
