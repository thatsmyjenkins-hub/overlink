#!/usr/bin/env python3
"""Register Overlink with a PS5 via Link Device PIN; save wake credential."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

SECRETS = (
    Path(__file__).resolve().parents[1]
    / "firmware/overlink-core/include/av_secrets.h"
)
CRED_CACHE = Path(__file__).resolve().parents[1] / "tools/.ps5_psn_account.json"


def account_from_redirect(redirect_url: str) -> str:
    from pyremoteplay.oauth import get_user_account

    info = get_user_account(redirect_url.strip())
    if not info:
        raise SystemExit("OAuth failed — code may be expired; login again")
    acc = info.get("account_id") or info.get("user_rpid")
    if not acc:
        print("oauth response keys:", list(info.keys()), file=sys.stderr)
        raise SystemExit("No account_id/user_rpid in OAuth response")
    info["account_id"] = acc
    CRED_CACHE.write_text(json.dumps(info, indent=2) + "\n")
    return acc


def save_cred(cred: str) -> None:
    text = SECRETS.read_text()
    if "PS5_USER_CREDENTIAL" in text:
        text = re.sub(
            r'#define PS5_USER_CREDENTIAL ".*"',
            f'#define PS5_USER_CREDENTIAL "{cred}"',
            text,
        )
    else:
        text += f'\n#define PS5_USER_CREDENTIAL "{cred}"\n'
    SECRETS.write_text(text)
    print(f"Wrote PS5_USER_CREDENTIAL to {SECRETS}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.69")
    ap.add_argument("--pin", required=True, help="8-digit Link Device PIN")
    ap.add_argument(
        "--redirect",
        help="Browser redirect URL after PSN login (https://remoteplay.dl...)",
    )
    ap.add_argument("--account-id", help="Base64 PSN account id if already known")
    args = ap.parse_args()
    pin = re.sub(r"\D", "", args.pin)
    if len(pin) != 8:
        raise SystemExit("PIN must be 8 digits")

    if args.account_id:
        account = args.account_id
    elif args.redirect:
        account = account_from_redirect(args.redirect)
    elif CRED_CACHE.exists():
        cached = json.loads(CRED_CACHE.read_text())
        account = cached.get("account_id") or cached.get("user_rpid")
        if not account:
            raise SystemExit(f"Cached PSN file missing account_id: {CRED_CACHE}")
        print("Using cached PSN account_id")
    else:
        from pyremoteplay.oauth import get_login_url

        print("1) Open this URL, sign into PSN:")
        print(get_login_url())
        print("2) After redirect, copy the FULL browser URL and re-run:")
        print(f"   {sys.argv[0]} --pin {pin} --redirect 'PASTE_URL'")
        raise SystemExit(2)

    from pyremoteplay.ddp import get_status
    from pyremoteplay.register import register

    status = get_status(args.host)
    print("PS5 status:", status)
    if not status:
        raise SystemExit("PS5 not found — wake it / check IP")

    info = register(args.host, account, pin, timeout=5.0)
    print("register:", info)
    if not info:
        raise SystemExit(
            "Register failed. On PS5: Settings → System → Remote Play → Link Device, "
            "get a fresh PIN, then retry."
        )

    # Chiaki: user_credential is RP-RegistKey interpreted as hex → decimal string
    regist = (info.get("PS5-RegistKey") or info.get("RP-RegistKey") or info.get("PS4-RegistKey")
              or info.get("RegistKey") or info.get("regist-key"))
    if not regist:
        # dump keys for debugging
        raise SystemExit(f"No regist key in response keys={list(info)}")

    # If already hex-like, convert to int string; else use as-is if numeric
    regist = regist.strip()
    try:
        cred = str(int(regist, 16))
    except ValueError:
        cred = regist
    print("wake credential:", cred)
    save_cred(cred)


if __name__ == "__main__":
    main()
