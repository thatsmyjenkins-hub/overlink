#include "rf_ctrl.h"
#include "pins.h"
#include <RadioLib.h>
#include <SPI.h>

static Module *mod = nullptr;
static CC1101 *radio = nullptr;

static bool ready = false;
static bool sniffing = false;
static String lastErr = "not started";
static float freqMhz = 433.92f;
static int16_t lastRssi = 0;
static uint32_t rxCount = 0;
static uint8_t lastPacket[64];
static size_t lastPacketLen = 0;
static volatile bool rxFlag = false;

#if defined(ESP8266) || defined(ESP32)
IRAM_ATTR
#endif
static void onRx() { rxFlag = true; }

void rfBegin() {
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, CC1101_CS);
  mod = new Module(CC1101_CS, CC1101_GDO0, RADIOLIB_NC, CC1101_GDO2);
  radio = new CC1101(mod);

  // FSK defaults first for chip bring-up; OOK used in sniff mode
  int16_t st = radio->begin();
  if (st != RADIOLIB_ERR_NONE) {
    ready = false;
    lastErr = "CC1101 begin failed (" + String(st) +
              "). Check 3.3V, SPI: CS=5 SCK=18 MISO=19 MOSI=23 GDO0=26.";
    return;
  }

  radio->setFrequency(freqMhz);
  radio->setOOK(true);
  radio->setBitRate(3.3);
  radio->setRxBandwidth(270.0);
  radio->setOutputPower(10);
  radio->setCrcFiltering(false);
  // Garage remotes / raw OOK rarely use a fixed sync word — promiscuous RX.
  radio->setPromiscuousMode(true);

  radio->setGdo0Action(onRx, RISING);
  ready = true;
  lastErr = "ok";
}

void rfLoop() {
  if (!ready || !sniffing || !radio) return;
  if (!rxFlag) return;
  rxFlag = false;

  uint8_t buf[64];
  int16_t st = radio->readData(buf, sizeof(buf));
  if (st == RADIOLIB_ERR_NONE || st == RADIOLIB_ERR_CRC_MISMATCH) {
    size_t n = radio->getPacketLength();
    if (n > sizeof(lastPacket)) n = sizeof(lastPacket);
    if (n > 0) {
      memcpy(lastPacket, buf, n);
      lastPacketLen = n;
      lastRssi = radio->getRSSI();
      rxCount++;
    }
  }
  radio->startReceive();
}

bool rfOk() { return ready; }
String rfLastError() { return lastErr; }

bool rfSetFrequency(float mhz, String &detail) {
  if (!ready || !radio) {
    detail = lastErr;
    return false;
  }
  if (mhz < 300.0f || mhz > 928.0f) {
    detail = "Frequency out of range (300–928 MHz)";
    return false;
  }
  int16_t st = radio->setFrequency(mhz);
  if (st != RADIOLIB_ERR_NONE) {
    detail = "setFrequency failed (" + String(st) + ")";
    return false;
  }
  freqMhz = mhz;
  detail = "Tuned to " + String(mhz, 3) + " MHz";
  if (sniffing) radio->startReceive();
  return true;
}

bool rfStartSniff(String &detail) {
  if (!ready || !radio) {
    detail = lastErr;
    return false;
  }
  rxFlag = false;
  int16_t st = radio->startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    detail = "startReceive failed (" + String(st) + ")";
    return false;
  }
  sniffing = true;
  detail = "Sniffing @ " + String(freqMhz, 3) + " MHz (OOK)";
  return true;
}

bool rfStopSniff(String &detail) {
  sniffing = false;
  if (radio) radio->standby();
  detail = "Sniff stopped";
  return true;
}

bool rfReplayLast(String &detail) {
  if (!ready || !radio) {
    detail = lastErr;
    return false;
  }
  if (lastPacketLen == 0) {
    detail = "No RF packet captured yet — start sniff first";
    return false;
  }
  bool was = sniffing;
  sniffing = false;
  int16_t st = radio->transmit(lastPacket, lastPacketLen);
  if (was) {
    radio->startReceive();
    sniffing = true;
  }
  if (st != RADIOLIB_ERR_NONE) {
    detail = "Transmit failed (" + String(st) + ")";
    return false;
  }
  detail = "Replayed " + String(lastPacketLen) + " bytes @ " +
           String(freqMhz, 3) + " MHz";
  return true;
}

bool rfSelfTest(String &detail) {
  if (!ready || !radio) {
    detail = lastErr;
    return false;
  }
  // Read RSSI in RX briefly
  radio->standby();
  int16_t st = radio->startReceive();
  delay(30);
  lastRssi = radio->getRSSI();
  radio->standby();
  if (sniffing) radio->startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    detail = "RX test failed (" + String(st) + ")";
    return false;
  }
  detail = "PASS — CC1101 SPI OK, RSSI=" + String(lastRssi) + " dBm @ " +
           String(freqMhz, 3) + " MHz";
  return true;
}

bool rfWatchRssi(uint32_t ms, JsonObject obj) {
  if (!ready || !radio) {
    obj["ok"] = false;
    obj["error"] = lastErr;
    return false;
  }
  if (ms < 200) ms = 200;
  if (ms > 15000) ms = 15000;

  bool was = sniffing;
  sniffing = false;
  radio->standby();
  // Packet-mode getRSSI() only updates after a packet (often stuck at -74).
  // Direct RX reads the live RSSI register — needed for garage remotes.
  int16_t st = radio->receiveDirectAsync();
  if (st != RADIOLIB_ERR_NONE) {
    obj["ok"] = false;
    obj["error"] = "receiveDirectAsync failed (" + String(st) + ")";
    return false;
  }

  delay(30);
  int16_t peak = -128;
  int16_t floorRssi = 0;
  int32_t sum = 0;
  uint32_t n = 0;
  uint32_t spikes = 0;
  int16_t baseline = (int16_t)radio->getRSSI();
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    int16_t r = (int16_t)radio->getRSSI();
    if (r > peak) peak = r;
    if (n == 0 || r < floorRssi) floorRssi = r;
    sum += r;
    n++;
    if (r >= baseline + 12) spikes++;
    delay(2);
  }
  lastRssi = peak;

  radio->standby();
  // Leave promiscuous packet RX ready if sniff was active.
  radio->setPromiscuousMode(true);
  if (was) {
    radio->startReceive();
    sniffing = true;
  }

  int16_t avg = n ? (int16_t)(sum / (int32_t)n) : 0;
  int16_t delta = peak - baseline;
  bool hit = delta >= 12 || spikes >= 3;

  obj["ok"] = true;
  obj["freqMhz"] = freqMhz;
  obj["ms"] = (int)ms;
  obj["baseline"] = baseline;
  obj["floor"] = floorRssi;
  obj["avg"] = avg;
  obj["peak"] = peak;
  obj["delta"] = delta;
  obj["spikes"] = (int)spikes;
  obj["hit"] = hit;
  obj["samples"] = (int)n;
  return true;
}

void rfGetStatus(JsonObject obj) {
  obj["ok"] = ready;
  obj["error"] = lastErr;
  obj["freqMhz"] = freqMhz;
  obj["sniffing"] = sniffing;
  obj["rxCount"] = rxCount;
  obj["lastRssi"] = lastRssi;
  obj["hasPacket"] = lastPacketLen > 0;
  obj["packetLen"] = (int)lastPacketLen;
  JsonObject pins = obj["pins"].to<JsonObject>();
  pins["cs"] = CC1101_CS;
  pins["gdo0"] = CC1101_GDO0;
  pins["gdo2"] = CC1101_GDO2;
  pins["sck"] = PIN_SCK;
  pins["miso"] = PIN_MISO;
  pins["mosi"] = PIN_MOSI;
}

static String packetToHex(bool spaced) {
  String hex;
  for (size_t i = 0; i < lastPacketLen; i++) {
    if (lastPacket[i] < 16) hex += "0";
    hex += String(lastPacket[i], HEX);
    if (spaced && i + 1 < lastPacketLen) hex += " ";
  }
  hex.toUpperCase();
  return hex;
}

static bool parseHexBytes(const String &hexIn, uint8_t *out, size_t maxOut,
                          size_t &outLen, String &detail) {
  String hex = hexIn;
  hex.replace(" ", "");
  hex.replace(":", "");
  hex.replace("-", "");
  hex.toUpperCase();
  if (hex.length() < 2 || (hex.length() % 2) != 0) {
    detail = "Hex must be even-length byte string";
    return false;
  }
  outLen = hex.length() / 2;
  if (outLen > maxOut) {
    detail = "Payload too long (max " + String((int)maxOut) + " bytes)";
    return false;
  }
  for (size_t i = 0; i < outLen; i++) {
    char pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char *end = nullptr;
    unsigned long v = strtoul(pair, &end, 16);
    if (end == pair || *end) {
      detail = "Invalid hex";
      return false;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}

void rfGetLastPacket(JsonObject obj) {
  obj["ok"] = lastPacketLen > 0;
  obj["len"] = (int)lastPacketLen;
  obj["rssi"] = lastRssi;
  obj["freqMhz"] = freqMhz;
  obj["hex"] = packetToHex(true);
}

float rfFreqMhz() { return freqMhz; }
bool rfHasPacket() { return lastPacketLen > 0; }
String rfLastPacketHexCompact() { return packetToHex(false); }

String rfVaultPayloadFromLast() {
  if (lastPacketLen == 0) return "";
  return "RF:" + String(freqMhz, 3) + ":" + packetToHex(false);
}

bool rfTransmitHex(const String &hex, float mhz, String &detail) {
  if (!ready || !radio) {
    detail = lastErr;
    return false;
  }
  uint8_t buf[64];
  size_t n = 0;
  if (!parseHexBytes(hex, buf, sizeof(buf), n, detail)) return false;
  if (mhz > 0.0f) {
    if (!rfSetFrequency(mhz, detail)) return false;
  }
  bool was = sniffing;
  sniffing = false;
  int16_t st = radio->transmit(buf, n);
  if (was) {
    radio->startReceive();
    sniffing = true;
  }
  if (st != RADIOLIB_ERR_NONE) {
    detail = "Transmit failed (" + String(st) + ")";
    return false;
  }
  // Keep as last packet so Replay Last works too
  memcpy(lastPacket, buf, n);
  lastPacketLen = n;
  detail = "TX " + String((int)n) + " bytes @ " + String(freqMhz, 3) + " MHz";
  return true;
}

bool rfTransmitVaultPayload(const String &payload, String &detail) {
  // RF:<mhz>:<hex>
  if (!payload.startsWith("RF:")) {
    detail = "Unsupported RF payload (want RF:<mhz>:<hex>)";
    return false;
  }
  int c1 = payload.indexOf(':', 3);
  if (c1 < 0) {
    detail = "Bad RF payload";
    return false;
  }
  float mhz = payload.substring(3, c1).toFloat();
  String hex = payload.substring(c1 + 1);
  return rfTransmitHex(hex, mhz, detail);
}
