import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.join(__dirname, '..');
const PORT = 8766;
const BASE = `http://127.0.0.1:${PORT}`;

let child;

before(async () => {
  child = spawn('node', ['scripts/mock_intel_server.mjs', String(PORT)], {
    cwd: ROOT,
    stdio: 'pipe',
  });
  await new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error('mock server timeout')), 8000);
    child.stdout.on('data', (d) => {
      if (String(d).includes('[mock]')) {
        clearTimeout(t);
        resolve();
      }
    });
    child.on('error', reject);
  });
  await new Promise((r) => setTimeout(r, 200));
});

after(() => {
  if (child) child.kill('SIGTERM');
});

async function get(path) {
  const r = await fetch(`${BASE}${path}`, { cache: 'no-store' });
  return { status: r.status, json: r.headers.get('content-type')?.includes('json') ? await r.json() : null };
}

test('health endpoint', async () => {
  const { status, json } = await get('/api/health');
  assert.equal(status, 200);
  assert.equal(json.ok, true);
  assert.equal(json.fs, true);
});

test('static assets', async () => {
  for (const p of ['/', '/app.css', '/app.js']) {
    const r = await fetch(`${BASE}${p}`);
    assert.equal(r.status, 200, p);
  }
});

test('intel API endpoints', async () => {
  const paths = [
    '/api/intel/summary',
    '/api/intel/wifi',
    '/api/intel/hosts',
    '/api/intel/profiles',
    '/api/intel/events',
  ];
  for (const p of paths) {
    const { status, json } = await get(p);
    assert.equal(status, 200, p);
    assert.ok(json);
  }
});

test('summary schema', async () => {
  const { json } = await get('/api/intel/summary');
  for (const key of ['phase', 'hostCount', 'wifiCount', 'ip', 'progressKind']) {
    assert.ok(key in json, `missing ${key}`);
  }
});

test('ap detail by BSSID', async () => {
  const { status, json } = await get('/api/intel/ap?bssid=AA:BB:CC:DD:EE:01');
  assert.equal(status, 200);
  assert.equal(json.found, true);
  assert.ok(json.ap);
  assert.ok(Array.isArray(json.scenarios));
});

test('ap action signal_profile', async () => {
  const r = await fetch(
    `${BASE}/api/intel/ap/action?bssid=AA:BB:CC:DD:EE:01&action=signal_profile`,
    { method: 'POST' },
  );
  const json = await r.json();
  assert.equal(r.status, 200);
  assert.equal(json.ok, true);
  assert.ok(Array.isArray(json.findings));
});

test('host detail by IP', async () => {
  const { status, json } = await get('/api/intel/host?ip=192.168.1.100');
  assert.equal(status, 200);
  assert.equal(json.found, true);
  assert.ok(Array.isArray(json.cves));
  assert.ok(Array.isArray(json.services));
});

test('vulns endpoint', async () => {
  const { json } = await get('/api/intel/vulns?ip=192.168.1.100');
  assert.equal(json.found, true);
  assert.ok(json.count >= 1);
});

test('cve search', async () => {
  const { status, json } = await get('/api/intel/cve?q=Dell');
  assert.equal(status, 200);
  assert.ok(Array.isArray(json));
});

test('cve search rejects short query', async () => {
  const r = await fetch(`${BASE}/api/intel/cve?q=x`);
  assert.equal(r.status, 400);
});

test('intel export bundle', async () => {
  const r = await fetch(`${BASE}/api/intel/export`);
  assert.equal(r.status, 200);
  const json = await r.json();
  assert.equal(json.schema, 'cyd2-intel-export-v1');
  assert.ok(json.summary);
  assert.ok(json.hosts);
  assert.ok(json.events);
});

test('control status', async () => {
  const { status, json } = await get('/api/control/status');
  assert.equal(status, 200);
  assert.ok(Array.isArray(json.actions));
  assert.ok(json.actions.includes('lan_sweep'));
});

test('control action', async () => {
  const r = await fetch(`${BASE}/api/control?action=pause`, { method: 'POST' });
  const json = await r.json();
  assert.equal(r.status, 200);
  assert.equal(json.ok, true);
  assert.equal(json.action, 'pause');
});

test('intel text report', async () => {
  const r = await fetch(`${BASE}/api/intel/report`);
  assert.equal(r.status, 200);
  const text = await r.text();
  assert.ok(text.includes('CYBERDECK NET INTEL REPORT'));
  assert.ok(text.includes('LAN HOSTS'));
});
