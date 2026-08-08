(() => {
  const $ = (sel, el = document) => el.querySelector(sel);
  const view = $("#view");
  const banner = $("#banner");
  const toastEl = $("#toast");
  let status = null;
  let pollTimer = null;
  let setupStep = 0;

  const routes = ["status", "setup", "network", "ir", "rf", "vault", "diag", "help"];

  function route() {
    const h = (location.hash || "#/status").replace(/^#\/?/, "").split("?")[0];
    return routes.includes(h) ? h : "status";
  }

  function toast(msg, kind = "") {
    toastEl.textContent = msg;
    toastEl.className = "toast " + kind;
    clearTimeout(toast._t);
    toast._t = setTimeout(() => toastEl.classList.add("hidden"), 3200);
  }

  async function api(path, opts = {}) {
    const res = await fetch(path, {
      headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
      ...opts,
    });
    let data = {};
    try { data = await res.json(); } catch (_) {}
    if (!res.ok) throw new Error(data.message || ("HTTP " + res.status));
    return data;
  }

  async function post(path, body = {}) {
    return api(path, { method: "POST", body: JSON.stringify(body) });
  }

  function setNav() {
    const r = route();
    document.querySelectorAll(".nav a").forEach((a) => {
      a.classList.toggle("active", a.dataset.route === r);
    });
  }

  function showBanner(html, kind = "") {
    if (!html) {
      banner.className = "banner hidden";
      banner.innerHTML = "";
      return;
    }
    banner.className = "banner " + kind;
    banner.innerHTML = html;
  }

  function updateChrome() {
    if (!status) return;
    const w = status.wifi || {};
    const link = $("#linkState");
    if (w.staConnected) {
      link.textContent = "STA " + (w.mode || "UP");
      link.className = "pill ok";
    } else {
      link.textContent = "AP MODE";
      link.className = "pill warn";
    }
    $("#heapPill").textContent = "HEAP " + Math.round((status.freeHeap || 0) / 1024) + "K";
    const ipBits = [];
    if (w.staIp) ipBits.push("LAN " + w.staIp);
    if (w.apIp) ipBits.push("AP " + w.apIp);
    if (w.mdns && w.staConnected) ipBits.push(w.mdns.replace("http://", ""));
    $("#footIp").textContent = ipBits.join(" · ") || ("IP: " + (status.ip || "—"));
    $("#footNext").textContent = "NEXT: " + (status.next || "—");
    $("#btnFooterGo").onclick = () => {
      location.hash = status.nextRoute || "#/status";
    };

    if (!w.hasCreds) {
      showBanner(
        `<span><strong>Still on SoftAP.</strong> Join your home Wi‑Fi for easy access via <code>cyberdeck.local</code>. SoftAP stays as backup (open, no password).</span>
         <button type="button" class="primary" id="bannerNet">Configure Network</button>`
      );
      $("#bannerNet").onclick = () => (location.hash = "#/network");
    } else if (!status.setupDone) {
      showBanner(
        `<span><strong>Setup incomplete.</strong> Run the wizard so IR/RF are verified — no guessing later.</span>
         <button type="button" class="primary" id="bannerSetup">Open Setup</button>`
      );
      $("#bannerSetup").onclick = () => (location.hash = "#/setup");
    } else if (status.rf && status.rf.ok === false) {
      showBanner(
        `<span><strong>CC1101 offline.</strong> ${esc(status.rf.error || "Check wiring")}</span>
         <button type="button" id="bannerDiag">Diagnostics</button>`,
        "bad"
      );
      $("#bannerDiag").onclick = () => (location.hash = "#/diag");
    } else {
      showBanner("");
    }
  }

  function esc(s) {
    return String(s ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  async function refreshStatus() {
    try {
      status = await api("/api/status");
      updateChrome();
      return status;
    } catch (e) {
      $("#linkState").textContent = "LINK DOWN";
      $("#linkState").className = "pill bad";
      showBanner(
        `<span><strong>Can't reach CyberDeck API.</strong> Join open Wi‑Fi <code>CyberDeck-IRRF</code> → http://192.168.44.1 — or on home Wi‑Fi try http://cyberdeck.local</span>
         <button type="button" id="bannerRetry">Retry</button>`,
        "bad"
      );
      $("#bannerRetry").onclick = () => render();
      throw e;
    }
  }

  /* ---------- Views ---------- */

  function viewNetwork(s) {
    const w = (s && s.wifi) || {};
    view.innerHTML = `
      <h1>Network</h1>
      <p class="lead">Self-hosted UI (no login). Best path: SoftAP first → save home Wi‑Fi → reboot → open <strong>http://cyberdeck.local</strong>. SoftAP stays up as an open backup.</p>
      <div class="grid cols-2">
        <div class="panel">
          <h2>Current links</h2>
          <div class="kv">
            <span>Mode</span><span>${esc(w.mode || "—")}</span>
            <span>Home Wi‑Fi</span><span>${w.staConnected ? esc(w.staSsid) + " · " + esc(w.staIp) : (w.hasCreds ? "saved, not connected" : "not configured")}</span>
            <span>mDNS</span><span>${w.staConnected ? esc(w.mdns) : "after STA connect"}</span>
            <span>SoftAP</span><span>${esc(w.apSsid)} (open) · ${esc(w.apIp || "—")}</span>
            <span>UI auth</span><span class="ok">None (open)</span>
          </div>
          <p class="muted" style="margin-top:0.7rem">${esc(w.bleNote || "")}</p>
        </div>
        <div class="panel">
          <h2>Join home Wi‑Fi</h2>
          <label>Network (SSID)</label>
          <input id="wifiSsid" type="text" maxlength="32" placeholder="YourWiFiName" value="${esc(w.hasCreds && !w.staConnected ? w.staSsid : "")}" />
          <label style="margin-top:0.5rem">Password</label>
          <input id="wifiPass" type="password" maxlength="64" placeholder="router password (blank if open)" />
          <div class="btnrow">
            <button type="button" id="wifiScan">Scan networks</button>
            <button type="button" class="primary" id="wifiSave">Save & Reboot</button>
            <button type="button" class="danger" id="wifiClear">Forget Wi‑Fi</button>
          </div>
          <div id="wifiScanList" class="list" style="margin-top:0.75rem"></div>
          <p class="muted" id="wifiHint">After reboot: phone on same Wi‑Fi → http://cyberdeck.local — or still use SoftAP CyberDeck-IRRF.</p>
        </div>
      </div>`;

    const fillScan = async () => {
      const box = $("#wifiScanList");
      box.innerHTML = `<div class="muted">Scanning… (may briefly hiccup SoftAP)</div>`;
      try {
        const data = await api("/api/wifi/scan");
        const nets = data.networks || [];
        if (!nets.length) {
          box.innerHTML = `<div class="empty"><strong>No networks found</strong>Type SSID manually.</div>`;
          return;
        }
        box.innerHTML = "";
        nets.sort((a, b) => (b.rssi || -999) - (a.rssi || -999));
        nets.slice(0, 16).forEach((n) => {
          const row = document.createElement("div");
          row.className = "item";
          row.innerHTML = `<div><strong>${esc(n.ssid || "(hidden)")}</strong><div class="muted">${n.rssi} dBm · ${n.secure ? "secured" : "open"}</div></div>
            <button type="button">Use</button>`;
          row.querySelector("button").onclick = () => {
            $("#wifiSsid").value = n.ssid || "";
            $("#wifiPass").focus();
          };
          box.appendChild(row);
        });
      } catch (e) {
        box.innerHTML = `<span class="bad">${esc(e.message)}</span>`;
      }
    };
    $("#wifiScan").onclick = fillScan;
    $("#wifiSave").onclick = async () => {
      const ssid = $("#wifiSsid").value.trim();
      const pass = $("#wifiPass").value;
      if (!ssid) return toast("SSID required", "bad");
      try {
        const r = await post("/api/wifi/save", { ssid, pass });
        toast(r.message || "Rebooting…", "ok");
        $("#wifiHint").textContent = "Device rebooting. Rejoin home Wi‑Fi, then open http://cyberdeck.local — SoftAP CyberDeck-IRRF remains as backup.";
      } catch (e) { toast(e.message, "bad"); }
    };
    $("#wifiClear").onclick = async () => {
      if (!confirm("Forget saved Wi‑Fi and reboot to SoftAP-only?")) return;
      try {
        await post("/api/wifi/clear", {});
        toast("Cleared — rebooting to SoftAP", "ok");
      } catch (e) { toast(e.message, "bad"); }
    };
  }

  function viewStatus(s) {
    const ir = s.ir || {};
    const rf = s.rf || {};
    const w = s.wifi || {};
    view.innerHTML = `
      <h1>Status</h1>
      <p class="lead">Deck health at a glance. If something's red, the footer NEXT always has a path forward.</p>
      <div class="grid cols-3">
        <div class="panel">
          <h2>Access</h2>
          <div class="kv">
            <span>Mode</span><span>${esc(w.mode || "—")}</span>
            <span>Best URL</span><span>${esc(w.primaryUrl || "—")}</span>
            <span>SoftAP</span><span>${esc(w.apSsid)} · open</span>
          </div>
          <div class="btnrow">
            <button type="button" class="primary" data-go="#/network">Network</button>
          </div>
        </div>
        <div class="panel">
          <h2>IR</h2>
          <div class="kv">
            <span>RX / TX</span><span>GPIO ${ir.rxPin} / ${ir.txPin}</span>
            <span>Captures</span><span>${ir.captures ?? 0}</span>
            <span>Last</span><span>${esc(ir.lastButton || ir.lastValue || "—")}</span>
          </div>
          <div class="btnrow">
            <button type="button" data-go="#/ir">IR Console</button>
            <button type="button" data-go="#/diag">IR QA</button>
          </div>
        </div>
        <div class="panel">
          <h2>RF / Setup</h2>
          <div class="kv">
            <span>RF</span><span class="${rf.ok ? "ok" : "bad"}">${rf.ok ? "ONLINE" : "OFFLINE"}</span>
            <span>Vault</span><span>${s.vaultCount ?? 0}</span>
            <span>Setup</span><span class="${s.setupDone ? "ok" : "bad"}">${s.setupDone ? "COMPLETE" : "NEEDED"}</span>
          </div>
          <div class="btnrow">
            <button type="button" data-go="#/rf">RF Console</button>
            <button type="button" data-go="#/setup">${s.setupDone ? "Re-run Setup" : "Start Setup"}</button>
          </div>
        </div>
      </div>
      <div class="panel" style="margin-top:0.85rem">
        <h2>Recommended next</h2>
        <p class="muted" style="margin:0 0 0.6rem">${esc(s.next)}</p>
        <button type="button" class="primary" data-go="${esc(s.nextRoute || "#/setup")}">Continue</button>
      </div>`;
  }

  function viewSetup(s) {
    const steps = [
      { id: "power", title: "Power rails", body: "JUMP = <strong>3.3V</strong>. CC1101 on 3.3V only. IR LED anode via transistor from <strong>dedicated 5V</strong> (not the JUMP V rail)." },
      { id: "ir_wiring", title: "IR wiring", body: "" },
      { id: "ir_loopback", title: "IR loopback", body: "Aim TX LED at RX dome (2–10 cm), then run the test." },
      { id: "rf_wiring", title: "RF wiring", body: "CC1101: VCC 3.3V, GND, SI→23, SO→19, SCK→18, CSN→5, GDO0→26, GDO2→27. Antenna on." },
      { id: "rf_test", title: "CC1101 SPI test", body: "Confirms the radio answers over SPI and can enter RX." },
      { id: "finish", title: "Finish", body: "Mark setup complete, then open <strong>Network</strong> to join home Wi‑Fi (reboot → <code>http://cyberdeck.local</code>). SoftAP stays as open backup." },
    ];
    setupStep = Math.min(setupStep, steps.length - 1);
    const cur = steps[setupStep];
    const hw = s.hardware || { ledOhm: 47, baseOhm: 1000, notes: "" };
    const irWiringBody = `
      <div class="kv" style="margin-bottom:0.75rem">
        <span>IR RX</span><span>OUT → <strong>D14 S</strong>, VCC → V, GND → G</span>
        <span>IR TX</span><span>Boosted LED (not Dorhea module)</span>
        <span>D4 S</span><span>→ <strong>${esc(hw.baseOhm)}Ω</strong> → 2N2222 <strong>base</strong> (middle)</span>
        <span>GND</span><span>→ 2N2222 <strong>emitter</strong> (left)</span>
        <span>5V</span><span>→ <strong>${esc(hw.ledOhm)}Ω</strong> → LED anode (long) → cathode (short) → <strong>collector</strong> (right)</span>
      </div>
      <p class="muted" style="margin:0 0 0.75rem">Your build uses a <strong>${esc(hw.ledOhm)}Ω</strong> LED resistor (not 68Ω). Edit below if you change parts.</p>
      <div class="grid cols-2" style="margin-bottom:0.5rem">
        <div>
          <label>LED series resistor (Ω)</label>
          <input id="hwLedOhm" type="number" min="10" max="220" value="${esc(hw.ledOhm)}" />
        </div>
        <div>
          <label>Base resistor (Ω)</label>
          <input id="hwBaseOhm" type="number" min="220" max="4700" value="${esc(hw.baseOhm)}" />
        </div>
      </div>
      <label>Hardware notes (saved on device)</label>
      <input id="hwNotes" type="text" maxlength="120" placeholder="e.g. 2N2222 + Gikfun LED on 5V" value="${esc(hw.notes || "")}" />
      <div class="btnrow">
        <button type="button" id="hwSave">Save hardware config</button>
      </div>`;
    view.innerHTML = `
      <h1>Setup Wizard</h1>
      <p class="lead">Guided bring-up. Every step has an action — no dead ends. Wrong text? Edit and save on the IR wiring step.</p>
      <div class="steps">${steps.map((st, i) =>
        `<div class="step ${i === setupStep ? "on" : ""} ${i < setupStep ? "done" : ""}">${i + 1}. ${st.title}</div>`
      ).join("")}</div>
      <div class="panel">
        <h2>Step ${setupStep + 1}: ${cur.title}</h2>
        ${cur.id === "ir_wiring" ? irWiringBody : `<p>${cur.body}</p>`}
        <div id="setupResult" class="muted" style="min-height:1.2rem;margin:0.5rem 0"></div>
        <div class="btnrow">
          <button type="button" id="setupBack" ${setupStep === 0 ? "disabled" : ""}>Back</button>
          ${cur.id === "ir_loopback" ? `<button type="button" class="primary" id="setupIrQa">Run IR Loopback</button>` : ""}
          ${cur.id === "rf_test" ? `<button type="button" class="primary" id="setupRfTest">Run CC1101 Test</button>` : ""}
          ${cur.id === "finish"
            ? `<button type="button" class="primary" id="setupDone">Mark Setup Complete</button>
               <button type="button" data-go="#/network">Configure Wi‑Fi</button>
               <button type="button" id="setupReset">Reset Wizard</button>`
            : `<button type="button" class="primary" id="setupNext">Next</button>`}
          <button type="button" data-go="#/diag">Open Diagnostics</button>
        </div>
      </div>
      <p class="muted" style="margin-top:0.8rem">Setup flag: <span class="${s.setupDone ? "ok" : "bad"}">${s.setupDone ? "COMPLETE" : "INCOMPLETE"}</span>
      · LED R: <strong>${esc(hw.ledOhm)}Ω</strong></p>`;

    $("#setupBack").onclick = () => { setupStep--; render(); };
    const next = $("#setupNext");
    if (next) next.onclick = () => { setupStep++; render(); };
    const hwSave = $("#hwSave");
    if (hwSave) hwSave.onclick = async () => {
      try {
        const r = await post("/api/setup/hardware", {
          ledOhm: parseInt($("#hwLedOhm").value, 10) || 47,
          baseOhm: parseInt($("#hwBaseOhm").value, 10) || 1000,
          notes: $("#hwNotes").value || "",
        });
        toast(r.message || "Saved", "ok");
        render();
      } catch (e) { toast(e.message, "bad"); }
    };
    const irQa = $("#setupIrQa");
    if (irQa) irQa.onclick = async () => {
      const el = $("#setupResult");
      el.textContent = "Running…";
      try {
        const r = await post("/api/ir/qa");
        el.innerHTML = `<span class="ok">${esc(r.message)}</span>`;
        toast(r.message, "ok");
        setupStep++;
        setTimeout(render, 400);
      } catch (e) {
        el.innerHTML = `<span class="bad">${esc(e.message)}</span> · <a href="#/diag" style="color:var(--cyan)">Troubleshoot</a>`;
        toast(e.message, "bad");
      }
    };
    const rfT = $("#setupRfTest");
    if (rfT) rfT.onclick = async () => {
      const el = $("#setupResult");
      el.textContent = "Running…";
      try {
        const r = await post("/api/rf/test");
        el.innerHTML = `<span class="ok">${esc(r.message)}</span>`;
        toast(r.message, "ok");
        setupStep++;
        setTimeout(render, 400);
      } catch (e) {
        el.innerHTML = `<span class="bad">${esc(e.message)}</span> · <a href="#/diag" style="color:var(--cyan)">Troubleshoot</a>`;
        toast(e.message, "bad");
      }
    };
    const done = $("#setupDone");
    if (done) done.onclick = async () => {
      await post("/api/setup/complete");
      toast("Setup complete", "ok");
      location.hash = "#/status";
    };
    const reset = $("#setupReset");
    if (reset) reset.onclick = async () => {
      await post("/api/setup/reset");
      setupStep = 0;
      toast("Wizard reset", "ok");
      render();
    };
  }

  function viewIr() {
    view.innerHTML = `
      <h1>IR Console</h1>
      <p class="lead">Live learn feed + Vizio blasts. Point remotes at the black RX dome.</p>
      <div class="grid cols-2">
        <div class="panel">
          <h2>Live capture</h2>
          <div class="feed" id="irFeed"><div class="empty">Waiting for IR… press a remote button.</div></div>
          <div class="btnrow">
            <button type="button" id="irReplay">Replay Last</button>
            <button type="button" id="irSave">Save to Vault</button>
            <button type="button" data-go="#/vault">Open Vault</button>
          </div>
        </div>
        <div class="panel">
          <h2>Vizio quick blast</h2>
          <p class="muted">Uses your confirmed NEC codes. Aim boosted TX at TV.</p>
          <div class="btnrow">
            <button type="button" class="primary" data-viz="MUTE">Mute</button>
            <button type="button" data-viz="VOL+">Vol +</button>
            <button type="button" data-viz="VOL-">Vol −</button>
            <button type="button" data-viz="POWER">Power</button>
            <button type="button" data-viz="POWER_ON">On</button>
            <button type="button" data-viz="POWER_OFF">Off</button>
          </div>
          <div class="btnrow">
            <button type="button" data-go="#/diag">IR not working?</button>
            <button type="button" data-go="#/help">Use cases</button>
          </div>
        </div>
      </div>`;

    view.querySelectorAll("[data-viz]").forEach((btn) => {
      btn.onclick = async () => {
        try {
          const r = await post("/api/ir/vizio", { action: btn.dataset.viz });
          toast(r.message || "Sent", "ok");
        } catch (e) { toast(e.message, "bad"); }
      };
    });
    $("#irReplay").onclick = async () => {
      try { toast((await post("/api/ir/replay")).message, "ok"); }
      catch (e) { toast(e.message, "bad"); }
    };
    $("#irSave").onclick = async () => {
      try { toast((await post("/api/ir/save", {})).message, "ok"); }
      catch (e) { toast(e.message, "bad"); }
    };

    clearInterval(pollTimer);
    pollTimer = setInterval(async () => {
      if (route() !== "ir") return;
      try {
        const live = await api("/api/ir/live");
        const feed = $("#irFeed");
        if (!feed) return;
        if (live.lines && live.lines.length) {
          if (feed.querySelector(".empty")) feed.innerHTML = "";
          live.lines.forEach((line) => {
            const d = document.createElement("div");
            d.className = "line";
            d.textContent = "> " + line;
            feed.appendChild(d);
          });
          feed.scrollTop = feed.scrollHeight;
        }
      } catch (_) {}
    }, 700);
  }

  function viewRf(s) {
    const rf = (s && s.rf) || {};
    view.innerHTML = `
      <h1>RF Console</h1>
      <p class="lead">CC1101 sub-GHz sniff / replay. Start with a known band for your region.</p>
      ${rf.ok ? "" : `<div class="empty"><strong>Radio offline</strong>${esc(rf.error || "")}<div class="btnrow"><button type="button" data-go="#/diag">Fix in Diagnostics</button><button type="button" data-go="#/setup">Setup Wizard</button></div></div>`}
      <div class="grid cols-2">
        <div class="panel">
          <h2>Tune</h2>
          <label>Frequency (MHz)</label>
          <select id="rfFreq">
            <option value="315.000">315.000</option>
            <option value="433.920" selected>433.920</option>
            <option value="868.350">868.350</option>
            <option value="915.000">915.000</option>
          </select>
          <div class="btnrow">
            <button type="button" class="primary" id="rfApply">Apply Freq</button>
            <button type="button" id="rfSniffOn">Start Sniff</button>
            <button type="button" id="rfSniffOff">Stop</button>
          </div>
          <p class="muted" id="rfState">Sniffing: ${rf.sniffing ? "YES" : "NO"} · Packets: ${rf.rxCount ?? 0}</p>
        </div>
        <div class="panel">
          <h2>Last packet</h2>
          <div class="kv" id="rfPkt">
            <span>Hex</span><span class="muted">—</span>
            <span>RSSI</span><span class="muted">—</span>
          </div>
          <div class="btnrow">
            <button type="button" id="rfRefresh">Refresh</button>
            <button type="button" class="primary" id="rfReplay">Replay Last</button>
            <button type="button" id="rfSave">Save to Vault</button>
            <button type="button" data-go="#/vault">Vault</button>
          </div>
        </div>
      </div>`;

    const applyFreq = async () => {
      try {
        const mhz = parseFloat($("#rfFreq").value);
        toast((await post("/api/rf/freq", { mhz })).message, "ok");
      } catch (e) { toast(e.message, "bad"); }
    };
    $("#rfApply").onclick = applyFreq;
    $("#rfSniffOn").onclick = async () => {
      try { toast((await post("/api/rf/sniff", { on: true })).message, "ok"); render(); }
      catch (e) { toast(e.message, "bad"); }
    };
    $("#rfSniffOff").onclick = async () => {
      try { toast((await post("/api/rf/sniff", { on: false })).message, "ok"); render(); }
      catch (e) { toast(e.message, "bad"); }
    };
    const loadPkt = async () => {
      try {
        const p = await api("/api/rf/packet");
        $("#rfPkt").innerHTML = `
          <span>Hex</span><span>${esc(p.hex || "—")}</span>
          <span>RSSI</span><span>${p.rssi ?? "—"} dBm</span>
          <span>Len</span><span>${p.len ?? 0}</span>`;
      } catch (e) { toast(e.message, "bad"); }
    };
    $("#rfRefresh").onclick = loadPkt;
    $("#rfReplay").onclick = async () => {
      try { toast((await post("/api/rf/replay")).message, "ok"); }
      catch (e) { toast(e.message, "bad"); }
    };
    $("#rfSave").onclick = async () => {
      try {
        const name = prompt("Vault name", "RF capture") || "RF capture";
        toast((await post("/api/rf/save", { name })).message, "ok");
      } catch (e) { toast(e.message, "bad"); }
    };
    loadPkt();
    clearInterval(pollTimer);
    pollTimer = setInterval(() => { if (route() === "rf") loadPkt(); }, 1500);
  }

  async function viewVault() {
    let items = [];
    try { items = (await api("/api/vault")).items || []; } catch (_) {}
    view.innerHTML = `
      <h1>Signal Vault</h1>
      <p class="lead">Saved IR (and later RF) payloads. Empty vaults always offer a capture path.</p>
      ${items.length ? `<div class="list" id="vaultList"></div>` : `
        <div class="empty">
          <strong>Vault is empty</strong>
          Nothing stuck — capture something first.
          <div class="btnrow">
            <button type="button" class="primary" data-go="#/ir">Learn IR</button>
            <button type="button" data-go="#/rf">Sniff RF</button>
            <button type="button" data-go="#/help">See use cases</button>
          </div>
        </div>`}`;
    if (!items.length) return;
    const list = $("#vaultList");
    items.forEach((it) => {
      const row = document.createElement("div");
      row.className = "item";
      row.innerHTML = `
        <div>
          <strong>${esc(it.name)}</strong>
          <div class="muted">${esc(it.kind)} · ${esc(it.payload)}</div>
        </div>
        <div class="btnrow">
          <button type="button" data-act="play">Replay</button>
          <button type="button" class="danger" data-act="del">Delete</button>
        </div>`;
      row.querySelector('[data-act="play"]').onclick = async () => {
        try { toast((await post("/api/vault/replay", { id: it.id })).message, "ok"); }
        catch (e) { toast(e.message, "bad"); }
      };
      row.querySelector('[data-act="del"]').onclick = async () => {
        try { await post("/api/vault/delete", { id: it.id }); toast("Deleted", "ok"); render(); }
        catch (e) { toast(e.message, "bad"); }
      };
      list.appendChild(row);
    });
  }

  function viewDiag(s) {
    const rf = (s && s.rf) || {};
    const ir = (s && s.ir) || {};
    view.innerHTML = `
      <h1>Diagnostics</h1>
      <p class="lead">Self-tests + pin map. Failures link to the exact fix — never a blank wall.</p>
      <div class="grid cols-2">
        <div class="panel">
          <h2>Self-tests</h2>
          <div id="diagOut" class="muted">Run a test.</div>
          <div class="btnrow">
            <button type="button" class="primary" id="diagIr">IR Loopback QA</button>
            <button type="button" id="diagRf">CC1101 SPI Test</button>
            <button type="button" data-go="#/setup">Setup Wizard</button>
          </div>
        </div>
        <div class="panel">
          <h2>Pin map</h2>
          <div class="kv">
            <span>IR RX</span><span>D${ir.rxPin} S / V / G</span>
            <span>IR TX</span><span>D${ir.txPin} → 2N2222 → LED ← 5V (${esc((s.hardware && s.hardware.ledOhm) || 47)}Ω)</span>
            <span>CC1101 CS</span><span>D${rf.pins?.cs ?? 5}</span>
            <span>SPI</span><span>SCK ${rf.pins?.sck ?? 18} MISO ${rf.pins?.miso ?? 19} MOSI ${rf.pins?.mosi ?? 23}</span>
            <span>GDO0/2</span><span>${rf.pins?.gdo0 ?? 26} / ${rf.pins?.gdo2 ?? 27}</span>
            <span>CC1101 VCC</span><span class="bad">3.3V ONLY</span>
          </div>
        </div>
      </div>
      <div class="panel" style="margin-top:0.85rem">
        <h2>Common fixes</h2>
        <div class="list">
          <div class="usecase"><h3>IR loopback fail</h3><p>Aim LED at RX dome. Confirm D4/D14. LED series = 47Ω to 5V. Swap 2N2222 E↔C if needed. Phone camera should see purple on blast.</p></div>
          <div class="usecase"><h3>TV ignores IR</h3><p>Codes OK if learn works — get closer / check transistor + 47Ω on dedicated 5V. Use IR Console Mute.</p></div>
          <div class="usecase"><h3>CC1101 begin fail</h3><p>JUMP 3.3V. No 5V on radio. Reseat SPI. Antenna attached.</p></div>
        </div>
      </div>`;
    const out = $("#diagOut");
    $("#diagIr").onclick = async () => {
      out.textContent = "Running IR QA…";
      try {
        const r = await post("/api/ir/qa");
        out.innerHTML = `<span class="ok">${esc(r.message)}</span>`;
        toast(r.message, "ok");
      } catch (e) {
        out.innerHTML = `<span class="bad">${esc(e.message)}</span>`;
        toast(e.message, "bad");
      }
    };
    $("#diagRf").onclick = async () => {
      out.textContent = "Running RF test…";
      try {
        const r = await post("/api/rf/test");
        out.innerHTML = `<span class="ok">${esc(r.message)}</span>`;
        toast(r.message, "ok");
      } catch (e) {
        out.innerHTML = `<span class="bad">${esc(e.message)}</span>`;
        toast(e.message, "bad");
      }
    };
  }

  async function viewHelp() {
    let cases = [];
    try { cases = (await api("/api/usecases")).cases || []; } catch (_) {}
    view.innerHTML = `
      <h1>UX Use Cases</h1>
      <p class="lead">Mission cards — each has a goal, a screen, and an escape hatch if stuck.</p>
      <div class="list" id="ucList"></div>`;
    const list = $("#ucList");
    cases.forEach((c) => {
      const el = document.createElement("div");
      el.className = "usecase";
      el.innerHTML = `
        <h3>${esc(c.id.toUpperCase())} · ${esc(c.title)}</h3>
        <p><strong>Goal:</strong> ${esc(c.goal)}</p>
        <p><strong>If stuck:</strong> ${esc(c.ifStuck)}</p>
        <div class="btnrow"><button type="button" class="primary" data-go="${esc(c.route)}">Go</button></div>`;
      list.appendChild(el);
    });
  }

  async function render() {
    setNav();
    clearInterval(pollTimer);
    pollTimer = null;
    view.innerHTML = `<p class="muted">Syncing…</p>`;
    let s = null;
    try { s = await refreshStatus(); } catch (_) { return; }
    const r = route();
    if (r === "status") viewStatus(s);
    else if (r === "setup") viewSetup(s);
    else if (r === "network") viewNetwork(s);
    else if (r === "ir") viewIr();
    else if (r === "rf") viewRf(s);
    else if (r === "vault") await viewVault();
    else if (r === "diag") viewDiag(s);
    else if (r === "help") await viewHelp();

    view.querySelectorAll("[data-go]").forEach((btn) => {
      btn.onclick = () => { location.hash = btn.getAttribute("data-go"); };
    });
  }

  window.addEventListener("hashchange", render);
  render();
  setInterval(() => { refreshStatus().catch(() => {}); }, 5000);
})();
