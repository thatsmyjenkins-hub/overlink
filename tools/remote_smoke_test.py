#!/usr/bin/env python3
"""Overlink Core remote smoke / Device Lab client."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request

DEFAULT = "http://192.168.4.55"


def req(base: str, path: str, method: str = "GET", body: dict | None = None, timeout: float = 60):
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(
        base.rstrip("/") + path,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"} if body is not None else {},
    )
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def main() -> int:
    ap = argparse.ArgumentParser(description="Overlink remote smoke test")
    ap.add_argument("--base", default=DEFAULT, help="Core base URL")
    ap.add_argument("--lab", action="store_true", help="Run /api/lab/smoke on Core")
    ap.add_argument("--scene", help="Run a scene id (e.g. home-normal)")
    ap.add_argument("--probe", action="store_true", help="Probe devices")
    ap.add_argument("--deck", help="CyberDeck Vizio action via Core (MUTE/POWER/VOL+/VOL-)")
    args = ap.parse_args()

    try:
        status = req(args.base, "/api/status", timeout=8)
    except Exception as e:
        print(f"FAIL status: {e}")
        return 2

    print("STATUS", json.dumps(status, indent=2))

    if args.probe or args.lab:
        print("PROBE", req(args.base, "/api/devices/probe", method="POST", timeout=45))
        print("DEVICES", json.dumps(req(args.base, "/api/devices", timeout=10), indent=2))

    if args.scene:
        print("SCENE", req(args.base, "/api/scenes/run", method="POST", body={"id": args.scene}))

    if args.deck:
        print("DECK", req(args.base, "/api/deck/ir", method="POST", body={"action": args.deck}))

    if args.lab:
        print("LAB", json.dumps(req(args.base, "/api/lab/smoke", method="POST", timeout=90), indent=2))

    return 0


if __name__ == "__main__":
    sys.exit(main())
