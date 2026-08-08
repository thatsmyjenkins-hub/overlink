#!/usr/bin/env python3
"""
Basement Controller QA — runs the same network commands the CYD firmware uses.
Exit code 0 = all critical tests passed.
"""

from __future__ import annotations

import json
import socket
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field

WIZ_PORT = 38899

WIZ_BULBS = [
    ("Back Left", "6c29900ca2e4", "192.168.4.43"),
    ("Back Right", "6c299007c19f", "192.168.4.44"),
    ("Front Left", "6c29900bc4f6", "192.168.4.45"),
    ("Front Right", "6c29900a5fe6", "192.168.4.47"),
]
WLED_IP = "192.168.5.162"
WLED_MAC = "40915150c486"
WIZ_SCENE_PARTY = 4
WIZ_SCENE_PARTY_DIMMING = 100
WIZ_SCENE_PARTY_SPEED = 100
WIZ_CMD_GAP_S = 0.08  # match firmware sendAll pacing
WIZ_RETRY_DELAY_S = 0.15
WIZ_RETRIES = 3


@dataclass
class Result:
    name: str
    passed: bool
    detail: str = ""


@dataclass
class QAReport:
    results: list[Result] = field(default_factory=list)

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        self.results.append(Result(name, ok, detail))
        mark = "PASS" if ok else "FAIL"
        line = f"  [{mark}] {name}"
        if detail:
            line += f" — {detail}"
        print(line)

    def exit_code(self) -> int:
        fails = [r for r in self.results if not r.passed]
        print(f"\n{'=' * 50}")
        print(f"  {len(self.results) - len(fails)}/{len(self.results)} passed")
        if fails:
            print("  FAILED:")
            for f in fails:
                print(f"    - {f.name}: {f.detail}")
            return 1
        print("  All automated tests passed.")
        return 0


def wiz_ok(resp: dict | None) -> bool:
    if not resp:
        return False
    if resp.get("success"):
        return True
    return bool(resp.get("result", {}).get("success"))


def wiz_cmd(ip: str, params: dict, timeout: float = 1.5) -> dict | None:
    msg = json.dumps({"method": "setPilot", "params": params}).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(msg, (ip, WIZ_PORT))
        data, _ = sock.recvfrom(4096)
        return json.loads(data.decode())
    except Exception:
        return None
    finally:
        sock.close()


def wiz_cmd_retry(ip: str, params: dict) -> dict | None:
    for attempt in range(WIZ_RETRIES):
        r = wiz_cmd(ip, params)
        if wiz_ok(r):
            return r
        if attempt + 1 < WIZ_RETRIES:
            time.sleep(WIZ_RETRY_DELAY_S)
    return None


def wled_post(ip: str, state: dict, timeout: float = 4.0, retries: int = 3) -> bool:
    body = json.dumps(state).encode()
    for attempt in range(retries):
        req = urllib.request.Request(
            f"http://{ip}/json/state",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                if resp.status == 200:
                    return True
        except Exception:
            pass
        if attempt + 1 < retries:
            time.sleep(0.2)
    return False


def wiz_get_pilot(ip: str) -> dict | None:
    msg = json.dumps({"method": "getPilot", "params": {}}).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.5)
    try:
        sock.sendto(msg, (ip, WIZ_PORT))
        data, _ = sock.recvfrom(4096)
        return json.loads(data.decode())
    except Exception:
        return None
    finally:
        sock.close()


def wiz_mac(ip: str) -> str | None:
    r = wiz_get_pilot(ip)
    if not r:
        return None
    return r.get("result", {}).get("mac")


def wled_get_state(ip: str) -> dict | None:
    try:
        with urllib.request.urlopen(f"http://{ip}/json/state", timeout=4) as resp:
            return json.loads(resp.read())
    except Exception:
        return None


def wled_get_info(ip: str) -> dict | None:
    try:
        with urllib.request.urlopen(f"http://{ip}/json/info", timeout=2) as resp:
            return json.loads(resp.read())
    except Exception:
        return None


def discover_wiz_broadcast() -> dict[str, str]:
    found: dict[str, str] = {}
    payload = json.dumps({"method": "getPilot", "params": {}}).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(0.4)
    for _ in range(3):
        for target in ("255.255.255.255", "192.168.4.255", "192.168.5.255"):
            try:
                sock.sendto(payload, (target, WIZ_PORT))
            except OSError:
                pass
        end = time.time() + 2.5
        while time.time() < end:
            try:
                data, addr = sock.recvfrom(4096)
                body = json.loads(data.decode())
                mac = body.get("result", {}).get("mac", "")
                if mac:
                    found[addr[0]] = mac
            except (socket.timeout, TimeoutError):
                break
            except Exception:
                pass
    sock.close()
    return found


def test_discovery(report: QAReport) -> None:
    print("\n── Discovery ──")
    broadcast = discover_wiz_broadcast()
    report.check("WiZ UDP broadcast", len(broadcast) > 0, f"{len(broadcast)} bulb(s) replied")

    for name, mac, ip in WIZ_BULBS:
        live_mac = wiz_mac(ip)
        ok = live_mac is not None and live_mac.lower() == mac.lower()
        report.check(f"WiZ fallback IP {name}", ok, f"{ip} mac={live_mac or 'no reply'}")

        matched = any(m.lower() == mac.lower() for m in broadcast.values())
        report.check(f"WiZ broadcast sees {name}", matched or ok,
                      "seen in broadcast" if matched else "unicast only (OK if on other subnet)")

    info = wled_get_info(WLED_IP)
    report.check("WLED reachable", info is not None, WLED_IP)
    if info:
        mac = info.get("mac", "")
        report.check("WLED MAC match", mac.lower() == WLED_MAC.lower(), mac)
        report.check("WLED LED count", info.get("leds", {}).get("count", 0) > 0,
                      str(info.get("leds", {}).get("count")))


def test_scenes(report: QAReport) -> None:
    print("\n── Scene commands (firmware-equivalent) ──")

    # Normal
    for name, _mac, ip in WIZ_BULBS:
        r = wiz_cmd_retry(ip, {"state": True, "dimming": 100, "temp": 3000})
        report.check(f"Scene Normal → {name}", wiz_ok(r), str(r))
        time.sleep(WIZ_CMD_GAP_S)
    report.check("Scene Normal → WLED", wled_post(WLED_IP, {
        "on": True, "bri": 200, "seg": [{"fx": 0, "col": [[255, 200, 140]]}]
    }))
    time.sleep(1.0)

    # Party
    for name, _mac, ip in WIZ_BULBS:
        r = wiz_cmd_retry(ip, {
            "state": True,
            "sceneId": WIZ_SCENE_PARTY,
            "dimming": WIZ_SCENE_PARTY_DIMMING,
            "speed": WIZ_SCENE_PARTY_SPEED,
        })
        report.check(f"Scene Party → {name}", wiz_ok(r), str(r))
        time.sleep(WIZ_CMD_GAP_S)
    report.check("Scene Party → WLED", wled_post(WLED_IP, {
        "on": True, "bri": 200, "seg": [{"fx": 9, "sx": 180, "ix": 128}]
    }))
    time.sleep(1.0)

    # Night
    for name, _mac, ip in WIZ_BULBS:
        r = wiz_cmd_retry(ip, {"state": True, "dimming": 10, "temp": 2200})
        report.check(f"Scene Night → {name}", wiz_ok(r), str(r))
        time.sleep(WIZ_CMD_GAP_S)
    report.check("Scene Night → WLED", wled_post(WLED_IP, {
        "on": True, "bri": 25, "seg": [{"fx": 2, "sx": 40, "ix": 40}]
    }))
    time.sleep(1.0)

    # All off
    for name, _mac, ip in WIZ_BULBS:
        r = wiz_cmd_retry(ip, {"state": False})
        report.check(f"Scene Off → {name}", wiz_ok(r), str(r))
        time.sleep(WIZ_CMD_GAP_S)
    report.check("Scene Off → WLED", wled_post(WLED_IP, {"on": False}))


def test_individual(report: QAReport) -> None:
    print("\n── Individual control ──")
    name, _mac, ip = WIZ_BULBS[0]
    wiz_cmd_retry(ip, {"state": True, "dimming": 80, "temp": 3000})
    time.sleep(0.5)
    state = wiz_get_pilot(ip)
    dim = state.get("result", {}).get("dimming") if state else None
    report.check(f"Individual dim {name}", dim is not None and dim >= 70, f"dimming={dim}")
    wiz_cmd_retry(ip, {"state": False})
    time.sleep(0.3)
    state = wiz_get_pilot(ip)
    on = state.get("result", {}).get("state") if state else True
    report.check(f"Individual off {name}", state is not None and not on, f"state={on}")

    wled_post(WLED_IP, {"on": True, "bri": 128})
    time.sleep(0.5)
    wled_post(WLED_IP, {"on": True, "bri": 200})
    time.sleep(0.3)
    st = wled_get_state(WLED_IP)
    bri = st.get("bri") if st else None
    report.check("Individual WLED brightness", bri is not None and bri >= 180, f"bri={bri}")
    wled_post(WLED_IP, {"on": False})


def test_cross_subnet(report: QAReport) -> None:
    print("\n── Network topology ──")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        local = sock.getsockname()[0]
    finally:
        sock.close()
    report.check("QA machine has IP", bool(local), local)

    bulbs_subnet = ".".join(WIZ_BULBS[0][2].split(".")[:3])
    wled_subnet = ".".join(WLED_IP.split(".")[:3])
    qa_subnet = ".".join(local.split(".")[:3])

    report.check("Bulbs on 192.168.4.x", bulbs_subnet == "192.168.4", bulbs_subnet)
    report.check("WLED on 192.168.5.x", wled_subnet == "192.168.5", wled_subnet)

    if qa_subnet != bulbs_subnet:
        print(f"  [NOTE] QA machine ({qa_subnet}.x) differs from bulbs ({bulbs_subnet}.x)")
        print("         CYD must route to both subnets — fallback IPs are critical for WiZ")
    if qa_subnet != wled_subnet:
        print(f"  [NOTE] QA machine ({qa_subnet}.x) differs from WLED ({wled_subnet}.x)")


def main() -> int:
    print("Basement Controller — Automated QA")
    print("=" * 50)
    report = QAReport()
    test_cross_subnet(report)
    test_discovery(report)
    test_scenes(report)
    test_individual(report)
    return report.exit_code()


if __name__ == "__main__":
    sys.exit(main())
