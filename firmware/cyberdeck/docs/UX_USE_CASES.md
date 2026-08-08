# CyberDeck UX Use Cases

Design rule: **no dead ends**. Every screen exposes a next action, and every failure links to Diagnostics or Setup.

| ID | Title | Goal | Primary route | If stuck |
|---|---|---|---|---|
| UC1 | First boot setup | Verify power, IR, RF | `#/setup` | Diagnostics IR/RF tests |
| UC2 | Control Vizio TV | Mute / vol / power | `#/ir` | Learn real remote → Save → Replay |
| UC3 | Learn unknown remote | Capture + name in vault | `#/ir` | Aim at RX dome; watch live feed |
| UC4 | Sniff sub-GHz fob | Tune → sniff → replay | `#/rf` | 3.3V + antenna; try 315/433/868/915 |
| UC5 | Replay saved signal | Transmit from vault | `#/vault` | Empty vault CTAs → IR/RF capture |
| UC6 | Troubleshoot no TX | Loopback + wiring | `#/diag` | Camera glow; swap 2N2222 E/C |
| UC7 | Troubleshoot RF dead | SPI self-test | `#/diag` | Never 5V on CC1101; reseat SPI |
| UC8 | Reconfigure after move | Reset + re-verify | `#/setup` | Setup → Reset wizard |

## Navigation map

```
Status ──► Setup wizard (if incomplete)
   ├─► IR Console ──► Vault
   ├─► RF Console ──► Vault
   ├─► Diagnostics (from any failure)
   └─► Use Cases (mission cards → deep links)
```

Footer **NEXT** + **GO →** always mirrors API `next` / `nextRoute`.
