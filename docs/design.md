# Overlink UI design

**Canonical reference:** `~/cyd-basement-control` (BASEMENT CTRL 3.5 / 2.8).

Do **not** use these for visual style (wrong era / wrong product):

- `~/Projects/BasementController` — purple LVGL tab UI
- `~/Projects/CYD2` / `firmware/grideye` — net-intel decks
- CyberDeck `data/app.css` Orbitron shell — ops peer, not WallDeck/phone chrome

## Palette (CTRL 3.5 / `CYD_UI_V2`)

| Token | Hex | Role |
|-------|-----|------|
| BG | `#0A1210` | Screen |
| PANEL | `#122018` | Button fill |
| CYAN | `#3DDC97` | Primary neon / borders / labels |
| AMBER | `#F0A030` | Accent scenes (DANCE/DATE/KARAOKE), READY |
| DIM | `#7A8F80` | Secondary text |
| ACTIVE | `#E8F5A0` | Selected tile fill |

## Chrome rules

- Portrait
- Sharp tiles (radius 3–6), 1px neon borders, no soft cards/shadows
- 2×5 scene grid: FULL CHILL / MOVIE GAME / SPORTS BED / DANCE DATE / KARAOKE OFF
- Amber border on DANCE, DATE, KARAOKE
- Active scene: ACTIVE fill + dark text
- Lower “Now” panel: zone | source, vol, apps, nav, DEVICES, commlink line

## Information architecture (post-CTRL)

- Phone: **Home · Zones · Scenes · Ops** (CTRL palette everywhere)
- WallDeck: **Home · Zones · Scenes** (Ops stays phone-only)
- **Home** = status dashboard (devices online, recent scene, peers)
- **Zones → Basement** = full CTRL (scene grid + Now / AV) — do not replace with a generic zone shell
- **Scenes** (top-level) = home-scoped only; zone scenes stay inside the zone
- WallDeck cold-start resumes last tab/zone from NVS
- Phone persists last tab/zone in localStorage
