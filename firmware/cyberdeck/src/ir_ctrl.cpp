#include "ir_ctrl.h"
#include "pins.h"
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

static const uint16_t kCaptureBufferSize = 1024;
static const uint8_t kIrTimeoutMs = 50;
static const uint16_t kMinUnknownSize = 12;
static const uint16_t kMaxRawSave = 512;
// Close-range: one NEC frame, no protocol repeats (mute is toggle — repeats flip it).
static const uint8_t kBoostFrames = 1;
static const uint16_t kNecExtraRepeats = 0;

static const uint16_t kVizioAddr = 0x04;

static IRrecv irrecv(IR_RX_PIN, kCaptureBufferSize, kIrTimeoutMs, true);
static IRsend irsend(IR_TX_PIN);

static decode_results results;
static decode_type_t lastType = decode_type_t::UNKNOWN;
static uint64_t lastValue = 0;
static uint16_t lastBits = 0;
static uint16_t lastRaw[kMaxRawSave];
static uint16_t lastRawLen = 0;
static bool haveLast = false;
static uint32_t captureCount = 0;
static String lastButton = "";
static String lastProto = "";
static String lastValueHex = "";

static const size_t kLiveCap = 24;
static String liveLog[kLiveCap];
static size_t liveHead = 0;
static size_t liveCount = 0;

static void livePush(const String &line) {
  liveLog[(liveHead + liveCount) % kLiveCap] = line;
  if (liveCount < kLiveCap) {
    liveCount++;
  } else {
    liveHead = (liveHead + 1) % kLiveCap;
  }
}

static const char *vizioName(uint16_t addr, uint16_t cmd) {
  if (addr != kVizioAddr) return nullptr;
  switch (cmd) {
    case 0x08: return "POWER";
    case 0x2A: return "POWER ON";
    case 0x25: return "POWER OFF";
    case 0x09: return "MUTE";
    case 0x02: return "VOL+";
    case 0x03: return "VOL-";
    case 0x00: return "CH+";
    case 0x01: return "CH-";
    case 0x2F: return "INPUT";
    case 0x2D: return "HOME";
    case 0x4A: return "BACK";
    case 0x44: return "OK";
    case 0x45: return "UP";
    case 0x46: return "DOWN";
    case 0x47: return "LEFT";
    case 0x48: return "RIGHT";
    default: return nullptr;
  }
}

static uint16_t vizioCmdFromAction(const String &a) {
  String u = a;
  u.toUpperCase();
  if (u == "POWER" || u == "P") return 0x08;
  if (u == "POWER_ON" || u == "ON" || u == "O") return 0x2A;
  if (u == "POWER_OFF" || u == "OFF" || u == "F") return 0x25;
  if (u == "MUTE" || u == "M") return 0x09;
  if (u == "VOL+" || u == "VOLUP" || u == "U") return 0x02;
  if (u == "VOL-" || u == "VOLDOWN" || u == "D") return 0x03;
  if (u == "INPUT") return 0x2F;
  if (u == "HOME") return 0x2D;
  if (u == "BACK") return 0x4A;
  if (u == "OK" || u == "ENTER" || u == "SELECT") return 0x44;
  if (u == "UP") return 0x45;
  if (u == "DOWN") return 0x46;
  if (u == "LEFT") return 0x47;
  if (u == "RIGHT") return 0x48;
  return 0xFFFF;
}

static void saveCapture(const decode_results &r) {
  lastType = r.decode_type;
  lastValue = r.value;
  lastBits = r.bits;
  lastProto = typeToString(r.decode_type);
  lastValueHex = "0x" + String((uint32_t)r.value, HEX);
  lastValueHex.toUpperCase();

  const char *name = nullptr;
  if (r.decode_type == decode_type_t::NEC) {
    name = vizioName((uint16_t)r.address, (uint16_t)r.command);
  }
  lastButton = name ? String("Vizio ") + name : "";

  uint16_t *raw = resultToRawArray(&r);
  uint16_t len = getCorrectedRawLength(&r);
  lastRawLen = 0;
  if (raw && len) {
    if (len > kMaxRawSave) len = kMaxRawSave;
    memcpy(lastRaw, raw, len * sizeof(uint16_t));
    lastRawLen = len;
    delete[] raw;
  }
  haveLast = true;
  captureCount++;

  String line = lastProto + " " + lastValueHex;
  if (lastButton.length()) line += " [" + lastButton + "]";
  livePush(line);
}

void irBegin() {
  irsend.begin();
  irrecv.enableIRIn();
}

void irLoop() {
  if (irrecv.decode(&results)) {
    if (results.rawlen >= kMinUnknownSize) {
      saveCapture(results);
    }
    irrecv.resume();
  }
}

bool irSendNec(uint32_t code, uint8_t frames) {
  if (frames < 1) frames = 1;
  if (frames > 2) frames = 2;  // hard cap — emitters are bright; never spray
  irrecv.disableIRIn();
  delay(5);
  for (uint8_t i = 0; i < frames; i++) {
    // 0 protocol-repeats: one optical burst per call
    irsend.sendNEC(code, kNECBits, kNecExtraRepeats);
    if (i + 1 < frames) delay(45);
  }
  delay(10);
  irrecv.enableIRIn();
  return true;
}

bool irSendSony(uint64_t data, uint16_t nbits, uint8_t frames) {
  if (nbits < 12) nbits = 12;
  if (nbits > 20) nbits = 20;
  if (frames < 1) frames = 1;
  if (frames > 2) frames = 2;
  irrecv.disableIRIn();
  delay(5);
  for (uint8_t i = 0; i < frames; i++) {
    // Library default Sony min-repeat is 2 (3 bursts). Force 0 extras.
    irsend.sendSony(data, nbits, /*repeat=*/0);
    if (i + 1 < frames) delay(45);
  }
  delay(10);
  irrecv.enableIRIn();
  return true;
}

bool irSendVizio(const String &action, String &detail) {
  uint16_t cmd = vizioCmdFromAction(action);
  if (cmd == 0xFFFF) {
    detail = "Unknown Vizio action";
    return false;
  }
  uint32_t code = irsend.encodeNEC(kVizioAddr, cmd);
  irSendNec(code, kBoostFrames);
  detail = "Sent Vizio " + action + " 0x" + String(code, HEX);
  return true;
}

bool irReplayLast(String &detail) {
  if (!haveLast) {
    detail = "No capture to replay — learn a remote first";
    return false;
  }
  irrecv.disableIRIn();
  delay(5);
  bool sent = false;
  // Prefer single-burst typed sends — irsend.send() uses protocol min-repeats
  // (Sony ≈ 3×) which makes the emitters look like they're "firing too much".
  if (lastType == decode_type_t::NEC) {
    irsend.sendNEC((uint32_t)lastValue, kNECBits, 0);
    sent = true;
  } else if (lastType == decode_type_t::SONY) {
    irsend.sendSony(lastValue, lastBits ? lastBits : 15, 0);
    sent = true;
  } else if (lastType != decode_type_t::UNKNOWN) {
    sent = irsend.send(lastType, lastValue, lastBits);
  }
  if (!sent && lastRawLen > 0) {
    irsend.sendRaw(lastRaw, lastRawLen, 38);
    sent = true;
  }
  delay(10);
  irrecv.enableIRIn();
  detail = "Replayed " + lastProto + " " + lastValueHex;
  return sent;
}

bool irLoopbackQa(String &detail) {
  uint32_t code = irsend.encodeNEC(kVizioAddr, 0x09);
  while (irrecv.decode(&results)) irrecv.resume();

  bool match = false;
  bool any = false;
  irsend.sendNEC(code);
  delay(40);
  irsend.sendNEC(code);

  uint32_t start = millis();
  while (millis() - start < 900) {
    if (irrecv.decode(&results)) {
      any = true;
      if (results.decode_type == decode_type_t::NEC &&
          ((uint32_t)results.value == code ||
           ((uint16_t)results.address == kVizioAddr &&
            (uint16_t)results.command == 0x09))) {
        match = true;
        saveCapture(results);
      }
      irrecv.resume();
      if (match) break;
    }
    delay(5);
  }

  if (match) {
    detail = "PASS — TX and RX optical path OK";
    return true;
  }
  if (any) {
    detail = "PARTIAL — RX heard IR but code mismatch. Check aim.";
    return false;
  }
  detail =
      "FAIL — RX heard nothing. Aim TX LED at RX dome, check D4/D14 wiring.";
  return false;
}

void irGetStatus(JsonObject obj) {
  obj["rxPin"] = IR_RX_PIN;
  obj["txPin"] = IR_TX_PIN;
  obj["captures"] = captureCount;
  obj["hasLast"] = haveLast;
  obj["lastProto"] = lastProto;
  obj["lastValue"] = lastValueHex;
  obj["lastButton"] = lastButton;
}

void irGetLastCapture(JsonObject obj) {
  obj["ok"] = haveLast;
  obj["proto"] = lastProto;
  obj["value"] = lastValueHex;
  obj["bits"] = lastBits;
  obj["button"] = lastButton;
  obj["rawLen"] = lastRawLen;
}

bool irHasCapture() { return haveLast; }

void irClearLiveLog() {
  liveHead = 0;
  liveCount = 0;
}

String irPopLiveLine() {
  if (!liveCount) return "";
  String s = liveLog[liveHead];
  liveHead = (liveHead + 1) % kLiveCap;
  liveCount--;
  return s;
}

size_t irLiveCount() { return liveCount; }
