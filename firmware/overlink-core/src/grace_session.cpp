#include "grace_session.h"

#include <string.h>

struct GraceState {
  char gameId[20];
  char gameName[28];
  char screen[12];  // menu|setup|ready|play|buzz|win
  char prompt[160];
  char detail[200];  // bans / answer / option B
  char blurb[48];
  char teamLabel[12];
  int scores[2];
  int timerSec;
  int remainSec;
  int endsAtMs;  // millis deadline mirrored from host (0 = none)
  bool reveal;
  bool kids;
  uint32_t updatedMs;
};

static GraceState g;

void graceSessionBegin() {
  memset(&g, 0, sizeof(g));
  strlcpy(g.screen, "menu", sizeof(g.screen));
  strlcpy(g.gameName, "Grace's Party Pack", sizeof(g.gameName));
  strlcpy(g.prompt, "Open Games on phone", sizeof(g.prompt));
  strlcpy(g.blurb, "second screen", sizeof(g.blurb));
  g.updatedMs = millis();
}

void graceSessionFill(JsonObject obj) {
  obj["ok"] = true;
  obj["gameId"] = g.gameId;
  obj["gameName"] = g.gameName;
  obj["screen"] = g.screen;
  obj["prompt"] = g.prompt;
  obj["detail"] = g.detail;
  obj["blurb"] = g.blurb;
  obj["teamLabel"] = g.teamLabel;
  obj["scores0"] = g.scores[0];
  obj["scores1"] = g.scores[1];
  obj["timerSec"] = g.timerSec;
  obj["endsAtMs"] = g.endsAtMs;
  obj["reveal"] = g.reveal;
  obj["kids"] = g.kids;
  obj["updatedMs"] = g.updatedMs;
  obj["remainSec"] = g.remainSec;
}

bool graceSessionUpdate(JsonVariantConst patch, String &message) {
  auto copy = [](char *dst, size_t n, JsonVariantConst v) {
    const char *s = v | "";
    strlcpy(dst, s, n);
  };
  if (!patch["gameId"].isNull()) copy(g.gameId, sizeof(g.gameId), patch["gameId"]);
  if (!patch["gameName"].isNull()) copy(g.gameName, sizeof(g.gameName), patch["gameName"]);
  if (!patch["screen"].isNull()) copy(g.screen, sizeof(g.screen), patch["screen"]);
  if (!patch["prompt"].isNull()) copy(g.prompt, sizeof(g.prompt), patch["prompt"]);
  if (!patch["detail"].isNull()) copy(g.detail, sizeof(g.detail), patch["detail"]);
  if (!patch["blurb"].isNull()) copy(g.blurb, sizeof(g.blurb), patch["blurb"]);
  if (!patch["teamLabel"].isNull()) copy(g.teamLabel, sizeof(g.teamLabel), patch["teamLabel"]);
  if (!patch["scores0"].isNull()) g.scores[0] = patch["scores0"] | 0;
  if (!patch["scores1"].isNull()) g.scores[1] = patch["scores1"] | 0;
  if (!patch["timerSec"].isNull()) g.timerSec = patch["timerSec"] | 0;
  if (!patch["endsAtMs"].isNull()) g.endsAtMs = patch["endsAtMs"] | 0;
  if (!patch["remainSec"].isNull()) g.remainSec = patch["remainSec"] | 0;
  if (!patch["reveal"].isNull()) g.reveal = patch["reveal"] | false;
  if (!patch["kids"].isNull()) g.kids = patch["kids"] | false;
  g.updatedMs = millis();
  message = "ok";
  return true;
}
