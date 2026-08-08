#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

struct CoreZone {
  char id[14];
  char name[18];
};

struct CoreDevice {
  char id[24];
  char zoneId[14];
  char type[12];
  char name[24];
  bool online;
};

struct CoreScene {
  char id[24];
  char name[20];
  char scope[8];
  char zoneId[16];
  bool confirm;
};

struct CoreTheme {
  char id[16];
  char name[16];
  uint32_t color;
};

class CoreClient {
 public:
  void begin();
  void loop();

  bool online() const { return coreOnline_; }
  const char *homeName() const { return homeName_; }
  IPAddress coreIp() const { return coreIp_; }

  size_t zoneCount() const { return zoneCount_; }
  const CoreZone &zone(size_t i) const { return zones_[i]; }

  size_t deviceCount() const { return deviceCount_; }
  const CoreDevice &device(size_t i) const { return devices_[i]; }

  size_t sceneCount() const { return sceneCount_; }
  const CoreScene &scene(size_t i) const { return scenes_[i]; }

  size_t themeCount() const { return themeCount_; }
  const CoreTheme &theme(size_t i) const { return themes_[i]; }

  bool runScene(const char *id, String &message);
  bool runTheme(const char *id, String &message);
  bool setDevice(const char *id, bool on, String &message, int dimming = -1);
  bool identify(const char *id, String &message);
  bool deckIr(const char *action, String &message);
  bool deckRf(const char *cmd, String &message);
  bool recovery(const char *action, String &message);
  bool probe();
  bool avApp(const char *id, String &message);
  bool avVol(int delta, int &levelOut, String &message);
  bool avWatch(String &message);
  bool avInput(const char *target, String &message);
  bool avKey(const char *name, String &message);

  bool fetchSummary(int &online, int &total, char *sceneTag, size_t tagLen, bool &deckOnline);

  // Grace Party Pack second-screen state from Core
  bool fetchGraceState(char *gameName, size_t gameLen, char *screen, size_t screenLen,
                       char *prompt, size_t promptLen, char *detail, size_t detailLen,
                       char *team, size_t teamLen, int &score0, int &score1, int &remainSec,
                       uint32_t *updatedMs = nullptr);

  // WLED via Core proxy
  bool fetchWledState(bool &on, int &bri, int &fx, char *name, size_t nameLen);
  bool wledSetJson(const char *json, String &message);

  // Party tricks via Core
  bool partySweep(String &message, int &mdnsCount, int &bleCount);
  bool partyBleStart(const char *message, bool cycle, String &outMsg);
  bool partyBleStop(String &message);
  bool partyStampede(String &message);
  bool partyCast(const char *message, String &outMsg);
  // Discover printers; optionally print to the first open one when printMsg non-null
  bool partyFindPrinters(String &message, int &count);
  bool partyPrintFirst(const char *message, String &outMsg);
  bool partyStatus(bool &advertising, bool &cycle, char *current, size_t currentLen);

  uint32_t stateHash() const;

 private:
  bool ensureCore();
  bool httpGet(const String &path, String &body, int timeoutMs = 2500);
  bool httpPost(const String &path, const String &json, String &body, int timeoutMs = 4000);
  bool refreshCatalog();
  bool refreshDevices();

  IPAddress coreIp_;
  bool coreOnline_ = false;
  uint8_t failStreak_ = 0;
  unsigned long lastPollMs_ = 0;
  char homeName_[40] = "OVERLINK";

  CoreZone zones_[MAX_ZONES];
  size_t zoneCount_ = 0;
  CoreDevice devices_[MAX_DEVICES];
  size_t deviceCount_ = 0;
  CoreScene scenes_[MAX_SCENES];
  size_t sceneCount_ = 0;
  CoreTheme themes_[12];
  size_t themeCount_ = 0;
};
