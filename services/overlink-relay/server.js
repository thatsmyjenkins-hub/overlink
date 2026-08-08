#!/usr/bin/env node
/**
 * Overlink relay — Core dials out over HTTP; phones attach via WebSocket.
 * No inbound ports required at the house.
 *
 * Env:
 *   PORT=8787
 *   RELAY_ADMIN_SECRET=change-me   (issues one-time enroll codes)
 */
const http = require("http");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");

const PORT = Number(process.env.PORT || 8787);
const ADMIN = process.env.RELAY_ADMIN_SECRET || "overlink-dev-secret";

/** @type {Map<string, {token:string, name:string, lastSeen:number, session:string, queue:any[], results:Map<string,any>}>} */
const grids = new Map();
/** one-time enroll codes → expires */
const enrollCodes = new Map();
/** phone sessions: sessionId → {gridId, ws} */
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

const server = http.createServer(async (req, res) => {
  if (req.method === "OPTIONS") return json(res, 204, {});

  const url = new URL(req.url || "/", `http://${req.headers.host}`);

  try {
    if (req.method === "GET" && url.pathname === "/health") {
      return json(res, 200, { ok: true, grids: grids.size, phones: phoneSessions.size });
    }

    // Admin: create enroll code
    if (req.method === "POST" && url.pathname === "/v1/admin/enroll-code") {
      const body = await readBody(req);
      if ((body.secret || req.headers.authorization || "") !== ADMIN &&
          body.secret !== ADMIN) {
        return json(res, 403, { ok: false, message: "forbidden" });
      }
      const code = mintCode();
      return json(res, 200, { ok: true, code, expiresInSec: 900 });
    }

    // Core enroll
    if (req.method === "POST" && url.pathname === "/v1/enroll") {
      const body = await readBody(req);
      const entry = enrollCodes.get(String(body.code || "").toUpperCase());
      if (!entry || entry.expires < Date.now()) {
        return json(res, 400, { ok: false, message: "invalid or expired code" });
      }
      enrollCodes.delete(String(body.code || "").toUpperCase());
      const gridId = String(body.gridId || "grid").slice(0, 48);
      const token = crypto.randomBytes(24).toString("hex");
      grids.set(gridId, {
        token,
        name: String(body.name || gridId),
        lastSeen: Date.now(),
        session: crypto.randomBytes(8).toString("hex"),
        queue: [],
        results: new Map(),
      });
      return json(res, 200, { ok: true, token, gridId });
    }

    // Core heartbeat
    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/heartbeat")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      auth.g.lastSeen = Date.now();
      auth.g.session = auth.g.session || crypto.randomBytes(8).toString("hex");
      return json(res, 200, { ok: true, session: auth.g.session });
    }

    // Core pull commands
    if (req.method === "GET" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/pull")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      auth.g.lastSeen = Date.now();
      const commands = auth.g.queue.splice(0, 16);
      return json(res, 200, { ok: true, commands });
    }

    // Core push result
    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/result")) {
      const auth = authGrid(req);
      if (!auth) return json(res, 401, { ok: false, message: "unauthorized" });
      const body = await readBody(req);
      const id = String(body.id || "");
      if (id) auth.g.results.set(id, body);
      // fan-out to phone sockets
      for (const [sid, ph] of phoneSessions) {
        if (ph.gridId === auth.gridId && ph.ws.readyState === 1) {
          ph.ws.send(JSON.stringify({ type: "result", result: body }));
        }
      }
      return json(res, 200, { ok: true });
    }

    // Phone: short-lived session token for a grid (requires grid token or admin)
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
      return json(res, 200, {
        ok: true,
        session,
        wsPath: `/v1/ws?session=${session}&grid=${encodeURIComponent(gridId)}`,
        expiresInSec: 3600,
      });
    }

    // List exposed grids (admin)
    if (req.method === "GET" && url.pathname === "/v1/admin/grids") {
      const secret = url.searchParams.get("secret") || "";
      if (secret !== ADMIN) return json(res, 403, { ok: false });
      const list = [...grids.entries()].map(([id, g]) => ({
        id,
        name: g.name,
        online: Date.now() - g.lastSeen < 15_000,
        lastSeen: g.lastSeen,
      }));
      return json(res, 200, { ok: true, grids: list });
    }

    // Revoke grid
    if (req.method === "POST" && url.pathname.startsWith("/v1/grids/") && url.pathname.endsWith("/revoke")) {
      const parts = url.pathname.split("/");
      const gridId = parts[3];
      const body = await readBody(req);
      const g = grids.get(gridId);
      if (!g || (body.token !== g.token && body.secret !== ADMIN)) {
        return json(res, 403, { ok: false });
      }
      grids.delete(gridId);
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
  const g = grids.get(gridId);
  if (!session || !g) {
    ws.close(4401, "unauthorized");
    return;
  }
  const sid = session;
  phoneSessions.set(sid, { gridId, ws });
  ws.send(JSON.stringify({ type: "hello", gridId, online: Date.now() - g.lastSeen < 15_000 }));

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

server.listen(PORT, () => {
  console.log(`[overlink-relay] http+ws on :${PORT}`);
  console.log(`[overlink-relay] admin secret set: ${ADMIN !== "overlink-dev-secret" ? "yes" : "default (dev)"}`);
});
