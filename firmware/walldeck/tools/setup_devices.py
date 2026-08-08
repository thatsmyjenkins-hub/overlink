#!/usr/bin/env python3
"""
Discover WiZ bulbs and WLED strips on the local network, blink each one
so you can identify it physically, then write names/IPs to src/config.h.
"""

from __future__ import annotations

import json
import re
import socket
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

WIZ_PORT = 38899
WLED_PORT = 80
SCAN_TIMEOUT = 0.35
WIZ_DISCOVER_TRIES = 3

ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = ROOT / "src" / "config.h"


@dataclass
class WizDevice:
    ip: str
    mac: str = ""
    module: str = ""


@dataclass
class WledDevice:
    ip: str
    name: str = ""
    led_count: int = 0


@dataclass
class SetupState:
    wiz: list[tuple[str, str]] = field(default_factory=list)  # (name, ip)
    wled_ip: str = ""
    wled_name: str = "WLED Strip"


def local_subnet() -> tuple[str, str]:
    """Return (local_ip, broadcast_ip) assuming a /24 network."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        local_ip = sock.getsockname()[0]
    finally:
        sock.close()

    parts = local_ip.split(".")
    if len(parts) != 4:
        raise RuntimeError(f"Unexpected local IP: {local_ip}")
    network = ".".join(parts[:3])
    return local_ip, f"{network}.255"


def wiz_send(ip: str, payload: dict, timeout: float = 0.5) -> dict | None:
    message = json.dumps(payload).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(message, (ip, WIZ_PORT))
        data, _ = sock.recvfrom(4096)
        return json.loads(data.decode())
    except (TimeoutError, socket.timeout, OSError, json.JSONDecodeError):
        return None
    finally:
        sock.close()


def discover_wiz() -> list[WizDevice]:
    local_ip, broadcast = local_subnet()
    print(f"  Local IP: {local_ip}")

    payload = json.dumps({"method": "getPilot", "params": {}}).encode()
    seen: dict[str, WizDevice] = {}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(SCAN_TIMEOUT)

    targets = [broadcast, "255.255.255.255"]
    for _ in range(WIZ_DISCOVER_TRIES):
        for target in targets:
            try:
                sock.sendto(payload, (target, WIZ_PORT))
            except OSError:
                pass
        deadline = time.time() + 2.0
        while time.time() < deadline:
            try:
                data, addr = sock.recvfrom(4096)
                ip = addr[0]
                if ip in seen:
                    continue
                body = json.loads(data.decode())
                result = body.get("result", {})
                mac = result.get("mac", "")
                seen[ip] = WizDevice(ip=ip, mac=mac)
            except (TimeoutError, socket.timeout):
                break
            except (OSError, json.JSONDecodeError):
                continue
    sock.close()

    # Fill in module name via getSystemConfig
    devices = list(seen.values())
    for dev in devices:
        resp = wiz_send(dev.ip, {"method": "getSystemConfig", "params": {}})
        if resp and "result" in resp:
            dev.module = resp["result"].get("moduleName", "")

    return sorted(devices, key=lambda d: [int(x) for x in d.ip.split(".")])


def probe_wled(ip: str) -> WledDevice | None:
    url = f"http://{ip}:{WLED_PORT}/json/info"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=SCAN_TIMEOUT) as resp:
            data = json.loads(resp.read().decode())
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError):
        return None

    if "leds" not in data and "ver" not in data:
        return None

    name = data.get("name") or data.get("brand") or "WLED"
    count = data.get("leds", {}).get("count", 0)
    return WledDevice(ip=ip, name=name, led_count=count)


def discover_wled() -> list[WledDevice]:
    _, broadcast = local_subnet()
    base = ".".join(broadcast.split(".")[:3])
    ips = [f"{base}.{host}" for host in range(1, 255)]

    found: list[WledDevice] = []
    with ThreadPoolExecutor(max_workers=48) as pool:
        futures = {pool.submit(probe_wled, ip): ip for ip in ips}
        for future in as_completed(futures):
            dev = future.result()
            if dev:
                found.append(dev)

    return sorted(found, key=lambda d: [int(x) for x in d.ip.split(".")])


def blink_wiz(ip: str, all_ips: list[str] | None = None, pulses: int = 5) -> None:
    """Blink one bulb bright white while others stay off — easy to spot in a room."""
    peers = all_ips or [ip]

    def send(target: str, params: dict) -> None:
        wiz_send(target, params, timeout=0.3)

    for _ in range(pulses):
        for peer in peers:
            send(peer, {"state": False})
        time.sleep(0.3)
        send(ip, {"state": True, "dimming": 100, "temp": 6500})
        time.sleep(0.9)
        send(ip, {"state": False})
        time.sleep(0.5)

    for peer in peers:
        send(peer, {"state": True, "dimming": 25, "temp": 2700})


def blink_wled(ip: str, pulses: int = 6) -> None:
    url = f"http://{ip}:{WLED_PORT}/json/state"
    headers = {"Content-Type": "application/json"}

    def post(body: dict) -> None:
        req = urllib.request.Request(
            url, data=json.dumps(body).encode(), headers=headers, method="POST"
        )
        try:
            urllib.request.urlopen(req, timeout=0.5)
        except (urllib.error.URLError, TimeoutError, OSError):
            pass

    for _ in range(pulses):
        post({"on": True, "bri": 255, "seg": [{"fx": 0, "col": [[255, 40, 40]]}]})
        time.sleep(0.35)
        post({"on": False})
        time.sleep(0.35)
    post({"on": True, "bri": 200, "seg": [{"fx": 0, "col": [[255, 200, 140]]}]})


def prompt(msg: str) -> str:
    try:
        return input(msg).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(0)


def identify_wiz_devices(devices: list[WizDevice], state: SetupState) -> None:
    if not devices:
        print("\nNo WiZ bulbs found. Make sure they are powered on and on the same WiFi.")
        return

    print(f"\nFound {len(devices)} WiZ bulb(s). We'll blink each one — watch the room.\n")
    all_ips = [d.ip for d in devices]
    assigned_ips: set[str] = set()

    for idx, dev in enumerate(devices, 1):
        mac_label = dev.mac or "unknown MAC"
        module_label = f" ({dev.module})" if dev.module else ""
        print(f"── Bulb {idx}/{len(devices)}: {dev.ip}  {mac_label}{module_label}")

        while True:
            choice = prompt("  [b] Blink  [n] Name this bulb  [s] Skip  > ").lower()
            if choice == "b":
                print("  Blinking bright white (others off)…")
                blink_wiz(dev.ip, all_ips=all_ips)
                print("  Done.")
            elif choice == "s":
                print("  Skipped.")
                break
            elif choice == "n":
                name = prompt("  Name for this bulb (e.g. Overhead): ")
                if not name:
                    print("  Name cannot be empty.")
                    continue
                state.wiz.append((name, dev.ip))
                assigned_ips.add(dev.ip)
                print(f"  Saved as '{name}'.")
                break
            else:
                print("  Enter b, n, or s.")


def identify_wled(devices: list[WledDevice], state: SetupState) -> None:
    if not devices:
        print("\nNo WLED devices found on the network.")
        manual = prompt("Enter WLED IP manually (or press Enter to skip): ")
        if manual:
            state.wled_ip = manual
            state.wled_name = prompt("Name for WLED strip [WLED Strip]: ") or "WLED Strip"
        return

    print(f"\nFound {len(devices)} WLED device(s):\n")
    for idx, dev in enumerate(devices, 1):
        leds = f", {dev.led_count} LEDs" if dev.led_count else ""
        print(f"  [{idx}] {dev.ip} — {dev.name}{leds}")

    if len(devices) == 1:
        dev = devices[0]
        print(f"\nUsing {dev.ip}.")
    else:
        while True:
            pick = prompt("Which WLED is your basement strip? Enter number: ")
            if pick.isdigit() and 1 <= int(pick) <= len(devices):
                dev = devices[int(pick) - 1]
                break
            print("  Invalid choice.")

    if prompt("  Blink this strip to verify? [Y/n] ").lower() not in ("n", "no"):
        print("  Blinking red…")
        blink_wled(dev.ip)
        print("  Done.")

    name = prompt(f"  Name for WLED [{dev.name}]: ") or dev.name
    state.wled_ip = dev.ip
    state.wled_name = name


def ip_to_array(ip: str) -> str:
    parts = ip.split(".")
    if len(parts) != 4:
        raise ValueError(f"Invalid IP: {ip}")
    return ", ".join(parts)


def write_config(state: SetupState) -> None:
    if not CONFIG_PATH.exists():
        raise FileNotFoundError(f"Missing {CONFIG_PATH}")

    text = CONFIG_PATH.read_text()

    if state.wiz:
        lines = []
        for name, ip in state.wiz:
            padded = f'"{name}",'.ljust(14)
            lines.append(f"    {{{padded} {{{ip_to_array(ip)}}}}},")
        bulbs_block = "static const WizBulbConfig WIZ_BULBS[] = {\n" + "\n".join(lines) + "\n};"
        text, count = re.subn(
            r"static const WizBulbConfig WIZ_BULBS\[\] = \{.*?\};",
            bulbs_block,
            text,
            count=1,
            flags=re.DOTALL,
        )
        if count != 1:
            raise RuntimeError("Could not update WIZ_BULBS in config.h")

    if state.wled_ip:
        text, count = re.subn(
            r'#define WLED_HOST "[^"]*"',
            f'#define WLED_HOST "{state.wled_ip}"',
            text,
            count=1,
        )
        if count != 1:
            raise RuntimeError("Could not update WLED_HOST in config.h")

    CONFIG_PATH.write_text(text)


def print_summary(state: SetupState) -> None:
    print("\n════════ Summary ════════")
    if state.wiz:
        print("WiZ bulbs:")
        for name, ip in state.wiz:
            print(f"  {name:16} {ip}")
    else:
        print("WiZ bulbs: (none assigned)")

    if state.wled_ip:
        print(f"WLED: {state.wled_name} @ {state.wled_ip}")
    else:
        print("WLED: (not set)")


def main() -> None:
    print("Basement Controller — device setup\n")
    print("Scanning for WiZ bulbs (UDP broadcast)…")
    wiz_devices = discover_wiz()
    print(f"  Found {len(wiz_devices)} WiZ bulb(s).")

    print("\nScanning for WLED strips (HTTP /json/info)…")
    wled_devices = discover_wled()
    print(f"  Found {len(wled_devices)} WLED device(s).")

    state = SetupState()
    identify_wiz_devices(wiz_devices, state)
    identify_wled(wled_devices, state)
    print_summary(state)

    if not state.wiz and not state.wled_ip:
        print("\nNothing to save.")
        return

    if prompt("\nWrite these to src/config.h? [Y/n] ").lower() in ("n", "no"):
        print("Not saved — run again when ready.")
        return

    write_config(state)
    print(f"\nUpdated {CONFIG_PATH}")
    print("Re-flash the CYD when ready:  pio run -t upload")


if __name__ == "__main__":
    main()
