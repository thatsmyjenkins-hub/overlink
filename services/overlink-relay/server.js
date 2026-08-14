#!/usr/bin/env node
/**
 * Overlink relay — Core dials out over HTTP; phones attach via WebSocket.
 *
 * Env:
 *   PORT=8080
 *   RELAY_ADMIN_SECRET=...
 *   RELAY_STATE_PATH=/tmp/overlink-relay-grids.json
 */
const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");

const PORT = Number(process.env.PORT || 8787);
const ADMIN = process.env.RELAY_ADMIN_SECRET || "overlink-dev-secret";
const STATE_PATH = process.env.RELAY_STATE_PATH || "/tmp/overlink-relay-grids.json";
const PUBLIC = path.join(__dirname, "public");

/** @type {Map<string, {token:string, name:string, lastSeen:number, session:string, queue:any[], results:Map<string,any>, catalog?:any}>} */
const grids = new Map();
const enrollCodes = new Map();
/** minted phone sessions: sessionId → {gridId, exp} */
const mintedSessions = new Map();
/** live sockets: sessionId → {gridId, ws} */
const phoneSessions = new Map();

function json(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type, X-Overlink-Token, X-Overlink-Grid, Authorization",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks).toString("utf8");
      if (!raw) return resolve({});
      try {
        resolve(JSON.parse(raw));
      } catch (e) {
        reject(e);
      }
    });
  });
}

function persist() {
  try {
    const payload = {
      grids: [...grids.entries()].map(([id, g]) => ({
        id,
        token: g.token,
        name: g.name,
        lastSeen: g.lastSeen,
        session: g.session,
        catalog: g.catalog || null,
      })),
    };
    fs.writeFileSync(STATE_PATH, JSON.stringify(payload));
  } catch (e) {
    console.error("[relay] persist fail", e.message);
  }
}

function restore() {
  try {
    if (!fs.existsSync(STATE_PATH)) return;
    const data = JSON.parse(fs.readFileSync(STATE_PATH, "utf8"));
    for (const row of data.grids || []) {
      grids.set(row.id, {
        token: row.token,
        name: row.name || row.id,
        lastSeen: row.lastSeen || 0,
        session: row.session || crypto.randomBytes(8).toString("hex"),
        queue: [],
        results: new Map(),
        catalog: row.catalog || null,
      });
    }
    console.log(`[relay] restored ${grids.size} grid(s) from ${STATE_PATH}`);
  } catch (e) {
    console.error("[relay] restore fail", e.message);
  }
}

function authGrid(req) {
  const token = req.headers["x-overlink-token"] || "";
  const gridId = req.headers["x-overlink-grid"] || "";
  const g = grids.get(gridId);
  if (!g || !token || g.token !== token) return null;
  return { gridId, g };
}

function mintCode() {
  const code = crypto.randomBytes(3).toString("hex").toUpperCase();
  enrollCodes.set(code, { expires: Date.now() + 15 * 60 * 1000 });
  return code;
}

function serveStatic(req, res, url) {
  let rel = url.pathname === "/" ? "/index.html" : url.pathname;
  if (rel.includes("..")) return false;
  const file = path.join(PUBLIC, rel);
  if (!file.startsWith(PUBLIC) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    return false;
  }
  const ext = path.extname(file);
  const types = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".json": "application/json",
  };
  res.writeHead(200, { "Content-Type": types[ext] || "application/octet-stream" });
  fs.createReadStream(file).pipe(res);
  return true;
}

restore();

const server = http.createServer(async (req, res) => {
  if (req.method === "OPTIONS") return json(res, 204, {});

  const url = new URL(req.url || "/", `http://${req.headers.host}`);

  try {
    if (req.method === "GET" && (url.pathname === "/" || !url.pathname.startsWith("/v1") && url.pathname !== "/health")) {
      if (serveStatic(req, res, url)) return;
    }

    if (req.method === "GET" && url.pathname === "/health") {
      return json(res, 200, {
        ok: true,
        grids: grids.size,
        phones: phoneSessions.size,
        persisted: fs.existsSync(STATE_PATH),
      });
    }

    if (req.method === "POST" && url.pathname === "/v1/admin/enroll-code") {
      const body = await readBody(req);
      if ((body.secret || req.headers.authorization || "") !== ADMIN && body.secret !== ADMIN) {
        return json(res, 403, { ok: false, message: "forbidden" });
      }
      const code = mintCode();
      return json(res, 200, { ok: true, code, expiresInSec: 900 });
    }

    if (req.method === "POST" && url.pathname === "/v1/enroll") {
      const body = await readBody(req);
      const entry = enrollCodes.get(String(body.code || "").toUpperCase());
      if (!entry || entry.expires < Date.now()) {
        return json(res, 400, { ok: false, message: "invalid or expired code" });
      }
      enrollCodes.delete(String(body.code || "").toUpperCase());
      const gridId = String(body.gridId || "grid").slice(0, 48);
      const existing = grids.get(gridId);
      const token = existing?.token || crypto.randomBytes(24).toString("hex");
      grids.set(gridId, {
        token,
        name: String(body.name || existing?.name || gridId),
        lastSeen: Date.now(),
        session: existing?.session || crypto.randomBytes(8).toString("hex"),
        queue: existing?.queue || [],
        results: existing?.results || new Map(),
        catalog: existing?.catalog || null,
      });
      persist();
      return json(res, 200, { ok: true, token, gridId, reused: Boolean(existing) });
    }

    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/heartbeat")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      const body = await readBody(req);
      auth.g.lastSeen = Date.now();
      auth.g.session = auth.g.session || crypto.randomBytes(8).toString("hex");
      if (body.catalog) auth.g.catalog = body.catalog;
      if (body.name) auth.g.name = String(body.name);
      persist();
      return json(res, 200, { ok: true, session: auth.g.session });
    }

    if (req.method === "GET" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/pull")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      auth.g.lastSeen = Date.now();
      const commands = auth.g.queue.splice(0, 16);
      return json(res, 200, { ok: true, commands });
    }

    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/result")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      const body = await readBody(req);
      const id = String(body.id || "");
      if (id) auth.g.results.set(id, body);
      for (const [, ph] of phoneSessions) {
        if (ph.gridId === auth.gridId && ph.ws.readyState === 1) {
          ph.ws.send(JSON.stringify({ type: "result", result: body }));
        }
      }
      return json(res, 200, { ok: true });
    }

    if (req.method === "GET" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/catalog")) {
      const parts = url.pathname.split("/");
      const gridId = parts[3];
      const secret = url.searchParams.get("secret") || "";
      const token = url.searchParams.get("token") || "";
      const g = grids.get(gridId);
      if (!g || (secret !== ADMIN && token !== g.token)) {
        return json(res, 403, { ok: false, message: "forbidden" });
      }
      return json(res, 200, {
        ok: true,
        gridId,
        name: g.name,
        online: Date.now() - g.lastSeen < 15_000,
        catalog: g.catalog || { devices: [], scenes: [] },
      });
    }

    if (req.method === "POST" && url.pathname === "/v1/session") {
      const body = await readBody(req);
      const gridId = String(body.gridId || "");
      const g = grids.get(gridId);
      if (!g || (body.token !== g.token && body.secret !== ADMIN)) {
        return json(res, 403, { ok: false, message: "forbidden" });
      }
      if (Date.now() - g.lastSeen > 60_000) {
        return json(res, 503, { ok: false, message: "grid offline" });
      }
      const session = crypto.randomBytes(16).toString("hex");
      mintedSessions.set(session, { gridId, exp: Date.now() + 3600 * 1000 });
      return json(res, 200, {
        ok: true,
        session,
        wsPath: `/v1/ws?session=${session}&grid=${encodeURIComponent(gridId)}`,
        expiresInSec: 3600,
        catalog: g.catalog || { devices: [], scenes: [] },
      });
    }

    if (req.method === "GET" && url.pathname === "/v1/admin/grids") {
      const secret = url.searchParams.get("secret") || "";
      if (secret !== ADMIN) return json(res, 403, { ok: false });
      const list = [...grids.entries()].map(([id, g]) => ({
        id,
        name: g.name,
        online: Date.now() - g.lastSeen < 15_000,
        lastSeen: g.lastSeen,
        devices: (g.catalog && g.catalog.devices && g.catalog.devices.length) || 0,
        scenes: (g.catalog && g.catalog.scenes && g.catalog.scenes.length) || 0,
      }));
      return json(res, 200, { ok: true, grids: list });
    }

    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/revoke")) {
      const parts = url.pathname.split("/");
      const gridId = parts[3];
      const body = await readBody(req);
      const g = grids.get(gridId);
      if (!g || (body.token !== g.token && body.secret !== ADMIN)) {
        return json(res, 403, { ok: false });
      }
      grids.delete(gridId);
      persist();
      return json(res, 200, { ok: true, message: "revoked" });
    }

    json(res, 404, { ok: false, message: "not found" });
  } catch (e) {
    json(res, 500, { ok: false, message: String(e.message || e) });
  }
});

const wss = new WebSocketServer({ server, path: "/v1/ws" });

wss.on("connection", (ws, req) => {
  const u = new URL(req.url || "/", `http://${req.headers.host}`);
  const session = u.searchParams.get("session") || "";
  const gridId = u.searchParams.get("grid") || "";
  const minted = mintedSessions.get(session);
  const g = grids.get(gridId);
  if (!session || !g || !minted || minted.gridId !== gridId || minted.exp < Date.now()) {
    ws.close(4401, "unauthorized");
    return;
  }
  const sid = session;
  phoneSessions.set(sid, { gridId, ws });
  ws.send(
    JSON.stringify({
      type: "hello",
      gridId,
      online: Date.now() - g.lastSeen < 15_000,
      catalog: g.catalog || { devices: [], scenes: [] },
    })
  );

  ws.on("message", (data) => {
    let msg;
    try {
      msg = JSON.parse(String(data));
    } catch {
      return;
    }
    if (msg.type === "command") {
      const id = msg.id || crypto.randomBytes(6).toString("hex");
      g.queue.push({
        id,
        op: msg.op,
        deviceId: msg.deviceId,
        sceneId: msg.sceneId,
        on: msg.on,
        dimming: msg.dimming,
      });
      ws.send(JSON.stringify({ type: "queued", id }));
    }
  });

  ws.on("close", () => phoneSessions.delete(sid));
});

setInterval(() => {
  const now = Date.now();
  for (const [sid, rec] of mintedSessions) {
    if (rec.exp < now) mintedSessions.delete(sid);
  }
}, 60_000);

server.listen(PORT, () => {
  console.log(`[overlink-relay] http+ws on :${PORT}`);
  console.log(`[overlink-relay] admin secret set: ${ADMIN !== "overlink-dev-secret" ? "yes" : "default (dev)"}`);
  console.log(`[overlink-relay] state ${STATE_PATH}`);
});
