#!/usr/bin/env node
/**
 * Mock CYBERDECK HTTP server for web app tests (no hardware required).
 * Usage: node scripts/mock_intel_server.mjs [port]
 */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.join(__dirname, '..');
const DATA = path.join(ROOT, 'data');
const FIX = path.join(ROOT, 'tests', 'fixtures');
const PORT = Number(process.argv[2] || process.env.CYD_MOCK_PORT || 8765);
let mockLanBusy = false;
let mockPaused = false;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json',
};

function readJson(name) {
  return fs.readFileSync(path.join(FIX, name), 'utf8');
}

function send(res, code, body, type = 'text/plain') {
  res.writeHead(code, {
    'Content-Type': type,
    'Access-Control-Allow-Origin': '*',
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

function handler(req, res) {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
  const p = url.pathname;

  if (p === '/api/health') {
    const s = JSON.parse(readJson('summary.json'));
    return send(res, 200, JSON.stringify({
      ok: true,
      ip: s.ip,
      ssid: s.ssid,
      rssi: s.rssi,
      heap: 200000,
      uptime: s.uptime,
      fs: true,
      hosts: s.hostCount,
    }), MIME['.json']);
  }

  if (p === '/api/intel/summary') return send(res, 200, readJson('summary.json'), MIME['.json']);
  if (p === '/api/intel/wifi') return send(res, 200, readJson('wifi.json'), MIME['.json']);
  if (p === '/api/intel/hosts') return send(res, 200, readJson('hosts.json'), MIME['.json']);
  if (p === '/api/intel/profiles') return send(res, 200, readJson('profiles.json'), MIME['.json']);
  if (p === '/api/intel/events') return send(res, 200, readJson('events.json'), MIME['.json']);

  if (p === '/api/intel/ap') {
    const bssid = (url.searchParams.get('bssid') || '').toUpperCase().replace(/[^0-9A-F:]/g, '');
    const safe = bssid.replace(/:/g, '-');
    const file = path.join(FIX, `ap-${safe}.json`);
    if (fs.existsSync(file)) return send(res, 200, fs.readFileSync(file, 'utf8'), MIME['.json']);
    return send(res, 200, JSON.stringify({ found: false, bssid }), MIME['.json']);
  }

  if (p === '/api/intel/ap/action') {
    const action = url.searchParams.get('action') || '';
    const bssid = url.searchParams.get('bssid') || '';
    const actionFile = path.join(FIX, `ap-action-${action}.json`);
    if (fs.existsSync(actionFile)) {
      const body = JSON.parse(fs.readFileSync(actionFile, 'utf8'));
      body.bssid = bssid;
      return send(res, 200, JSON.stringify(body), MIME['.json']);
    }
    return send(res, 400, JSON.stringify({ ok: false, message: 'Unknown action' }), MIME['.json']);
  }

  if (p === '/api/intel/host') {
    const ip = url.searchParams.get('ip') || '';
    const safe = ip.replace(/[^0-9.]/g, '');
    const file = path.join(FIX, `host-${safe}.json`);
    if (fs.existsSync(file)) return send(res, 200, fs.readFileSync(file, 'utf8'), MIME['.json']);
    return send(res, 200, JSON.stringify({ found: false, ip: safe }), MIME['.json']);
  }

  if (p === '/api/intel/vulns') {
    const ip = url.searchParams.get('ip') || '';
    const safe = ip.replace(/[^0-9.]/g, '');
    const hostFile = path.join(FIX, `host-${safe}.json`);
    if (fs.existsSync(hostFile)) {
      const h = JSON.parse(fs.readFileSync(hostFile, 'utf8'));
      return send(res, 200, JSON.stringify({
        found: h.found,
        ip: safe,
        cves: h.cves || [],
        count: h.count || 0,
        dbLoaded: h.dbLoaded ?? true,
      }), MIME['.json']);
    }
    return send(res, 200, JSON.stringify({ found: false }), MIME['.json']);
  }

  if (p === '/api/intel/cve') {
    const q = url.searchParams.get('q') || '';
    if (q.length < 2) return send(res, 400, JSON.stringify({ error: 'q too short' }), MIME['.json']);
    return send(res, 200, JSON.stringify([
      { id: 'CVE-2019-0708', summary: 'Mock CIRCL result for ' + q },
      { id: 'CVE-2020-1472', summary: 'Mock Zerologon' },
    ]), MIME['.json']);
  }

  if (p === '/api/intel/export') {
    const bundle = {
      schema: 'cyd2-intel-export-v1',
      exportedAt: Math.floor(Date.now() / 1000),
      dashboard: `http://127.0.0.1:${PORT}/`,
      summary: JSON.parse(readJson('summary.json')),
      wifi: JSON.parse(readJson('wifi.json')),
      hosts: JSON.parse(readJson('hosts.json')),
      profiles: JSON.parse(readJson('profiles.json')),
      events: JSON.parse(readJson('events.json')),
    };
    return send(res, 200, JSON.stringify(bundle), MIME['.json']);
  }

  if (p === '/api/intel/report') {
    const s = JSON.parse(readJson('summary.json'));
    const hosts = JSON.parse(readJson('hosts.json'));
    let text = 'CYBERDECK NET INTEL REPORT\n==========================\n\n';
    text += `Network: ${s.ssid || '—'}  IP: ${s.ip || '—'}\n`;
    text += `Hosts: ${s.hostCount || 0}\n\n--- LAN HOSTS ---\n`;
    for (const h of hosts.hosts || []) {
      text += `${h.ip}  risk=${h.risk ?? 0}\n`;
    }
    return send(res, 200, text, 'text/plain');
  }

  if (p === '/api/intel/log') {
    return send(res, 200, 'CYBERDECK intel engine started\nMock event line\n', 'text/plain');
  }

  if (p === '/api/control/status') {
    const s = JSON.parse(readJson('summary.json'));
    return send(res, 200, JSON.stringify({
      ok: true,
      phase: s.phase,
      sweepActive: false,
      orchestrationPaused: s.orchestrationPaused ?? false,
      hostCount: s.hostCount,
      profileQueue: s.profileQueue ?? 0,
      actions: ['lan_sweep', 'rf_scan', 'profile_all', 'profile_host', 'export', 'clear_discovery', 'clear_log', 'pause', 'resume', 'cancel_sweep', 'stop_profile', 'auto_profile_on', 'auto_profile_off'],
      autoProfile: false,
      multicastListen: true,
    }), MIME['.json']);
  }

  if (p === '/api/control') {
    const action = url.searchParams.get('action') || '';
    const ip = url.searchParams.get('ip') || '';
    const ok = action !== 'lan_sweep' || !mockLanBusy;
    if (action === 'lan_sweep') mockLanBusy = true;
    if (action === 'pause') mockPaused = true;
    if (action === 'resume') mockPaused = false;
    if (action === 'clear_discovery') mockLanBusy = false;
    return send(res, ok ? 200 : 409, JSON.stringify({
      ok,
      action,
      message: ok ? `Mock ${action}${ip ? ' ' + ip : ''} ok` : 'LAN sweep already running',
      phase: mockLanBusy ? 'LAN_SWEEP' : 'MONITOR',
    }), MIME['.json']);
  }

  const staticMap = {
    '/': '/index.html',
    '/index.html': '/index.html',
    '/app.css': '/app.css',
    '/app.js': '/app.js',
  };
  const rel = staticMap[p];
  if (rel) {
    const file = path.join(DATA, rel.replace(/^\//, ''));
    const ext = path.extname(file);
    return send(res, 200, fs.readFileSync(file), MIME[ext] || 'text/plain');
  }

  send(res, 404, 'Not found');
}

const server = http.createServer(handler);
server.listen(PORT, '127.0.0.1', () => {
  console.log(`[mock] CYBERDECK http://127.0.0.1:${PORT}/`);
});
