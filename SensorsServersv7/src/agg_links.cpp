#include "agg_links.hpp"

#if _IS_SERVER_HUB

#include "globals.hpp"
#include <string.h>
#ifdef _USESDCARD
#include <SD.h>
#endif
#ifndef _USESDCARD
#include <Preferences.h>
#endif

extern Devices_Sensors Sensors;

static const uint8_t AGG_VER = 1;
static const char* AGG_SD_PATH = "/Data/AggLinks.dat";

struct AggDeviceRec {
  uint8_t used;
  uint8_t devType;
  uint8_t ip[4];
  uint8_t pad;
  uint32_t sendingInt;
  uint64_t mac;
  char name[31];
};

struct AggLinkRec {
  uint8_t used;
  uint8_t snsType;
  uint8_t snsID;
  uint8_t localPrefs;
  uint64_t mac;
};

struct AggBlob {
  uint8_t ver;
  uint8_t pad[3];
  AggDeviceRec devs[AGG_MAX_DEVICES];
  AggLinkRec links[AGG_MAX_LINKS];
};

static AggBlob s_blob;
static bool s_loading = false;

static const char* kGroups[] = {
  "temperature", "humidity", "pressure", "soil", "leak", "battery", "distance",
  "binary", "human", "HVAC", "valve", "network", "altitude", "weather", "clock", "switch"
};

static AggDeviceRec* findDev(uint64_t mac) {
  if (mac == 0) return nullptr;
  for (uint8_t i = 0; i < AGG_MAX_DEVICES; i++) {
    if (s_blob.devs[i].used && s_blob.devs[i].mac == mac) return &s_blob.devs[i];
  }
  return nullptr;
}

static void copyIp(uint8_t dst[4], IPAddress ip) {
  dst[0] = ip[0];
  dst[1] = ip[1];
  dst[2] = ip[2];
  dst[3] = ip[3];
}

static bool ipEqual(const uint8_t dst[4], IPAddress ip) {
  return dst[0] == ip[0] && dst[1] == ip[1] && dst[2] == ip[2] && dst[3] == ip[3];
}

static bool readBlob(AggBlob& incoming) {
  memset(&incoming, 0, sizeof(incoming));
#ifdef _USESDCARD
  File f = SD.open(AGG_SD_PATH, FILE_READ);
  if (!f) return false;
  if (f.size() != sizeof(AggBlob)) {
    f.close();
    return false;
  }
  const size_t n = f.read((uint8_t*)&incoming, sizeof(incoming));
  f.close();
  return n == sizeof(incoming);
#else
  Preferences p;
  if (!p.begin("agglinks", true)) return false;
  const size_t n = p.getBytesLength("table");
  bool ok = false;
  if (n == sizeof(incoming)) {
    ok = p.getBytes("table", &incoming, sizeof(incoming)) == sizeof(incoming);
  }
  p.end();
  return ok;
#endif
}

void AggLinks_save() {
  s_blob.ver = AGG_VER;
#ifdef _USESDCARD
  File f = SD.open(AGG_SD_PATH, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)&s_blob, sizeof(s_blob));
  f.close();
#else
  Preferences p;
  if (!p.begin("agglinks", false)) return;
  p.putBytes("table", &s_blob, sizeof(s_blob));
  p.end();
#endif
}

bool AggLinks_hasDevice(uint64_t mac) {
  return findDev(mac) != nullptr;
}

bool AggLinks_addDevice(uint64_t mac, IPAddress ip, const char* name, uint8_t devType) {
  if (mac == 0) return false;
  AggDeviceRec* existing = findDev(mac);
  bool created = false;
  if (!existing) {
    for (uint8_t i = 0; i < AGG_MAX_DEVICES; i++) {
      if (!s_blob.devs[i].used) {
        existing = &s_blob.devs[i];
        memset(existing, 0, sizeof(*existing));
        existing->used = 1;
        existing->mac = mac;
        created = true;
        break;
      }
    }
  }
  if (!existing) return false;
  bool changed = created || existing->devType != devType;
  existing->devType = devType;
  if (ip != IPAddress(0, 0, 0, 0) && !ipEqual(existing->ip, ip)) {
    copyIp(existing->ip, ip);
    changed = true;
  }
  if (name && name[0]) {
    if (strncmp(existing->name, name, sizeof(existing->name)) != 0) {
      strncpy(existing->name, name, sizeof(existing->name) - 1);
      existing->name[sizeof(existing->name) - 1] = '\0';
      changed = true;
    }
  }
  if (changed && !s_loading) AggLinks_save();
  return true;
}

void AggLinks_noteDevice(uint64_t mac, IPAddress ip, uint32_t sendingInt) {
  if (s_loading) return;
  AggDeviceRec* d = findDev(mac);
  if (!d) return;
  bool changed = false;
  if (ip != IPAddress(0, 0, 0, 0) && !ipEqual(d->ip, ip)) {
    copyIp(d->ip, ip);
    changed = true;
  }
  if (sendingInt != 0 && d->sendingInt != sendingInt) {
    d->sendingInt = sendingInt;
    changed = true;
  }
  if (changed) AggLinks_save();
}

String AggLinks_groupName(uint8_t snsType) {
  for (uint8_t i = 0; i < sizeof(kGroups) / sizeof(kGroups[0]); i++) {
    if (Sensors.isSensorOfType(snsType, kGroups[i])) return String(kGroups[i]);
  }
  return String("sns") + String(snsType);
}

bool AggLinks_isPicked(uint8_t prefsIndex, uint64_t mac, uint8_t snsType, uint8_t snsID) {
  for (uint8_t i = 0; i < AGG_MAX_LINKS; i++) {
    const AggLinkRec& L = s_blob.links[i];
    if (!L.used || L.localPrefs != prefsIndex) continue;
    if (L.mac == mac && L.snsType == snsType && L.snsID == snsID) return true;
  }
  return false;
}

uint8_t AggLinks_linksForPrefs(uint8_t prefsIndex, AggPick* out, uint8_t maxOut) {
  uint8_t n = 0;
  if (!out || maxOut == 0) return 0;
  for (uint8_t i = 0; i < AGG_MAX_LINKS && n < maxOut; i++) {
    const AggLinkRec& L = s_blob.links[i];
    if (!L.used || L.localPrefs != prefsIndex) continue;
    out[n].mac = L.mac;
    out[n].snsType = L.snsType;
    out[n].snsID = L.snsID;
    n++;
  }
  return n;
}

bool AggLinks_setPicks(uint8_t prefsIndex, const AggPick* picks, uint8_t count, String& err) {
  err = "";
  if (count > AGG_MAX_LINKS) {
    err = "full";
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    if (!picks || picks[i].mac == 0) {
      err = "missing";
      return false;
    }
    if (AggLinks_hasDevice(picks[i].mac)) continue;
    const int16_t di = Sensors.findDevice(picks[i].mac);
    ArborysDevType* d = (di >= 0) ? Sensors.getDeviceByDevIndex(di) : nullptr;
    const bool added = (d && d->IsSet)
        ? AggLinks_addDevice(picks[i].mac, d->IP, d->devName, d->devType)
        : AggLinks_addDevice(picks[i].mac, IPAddress(0, 0, 0, 0), "manual", 0);
    if (!added) {
      err = "full";
      return false;
    }
  }
  uint8_t others = 0;
  for (uint8_t i = 0; i < AGG_MAX_LINKS; i++) {
    if (s_blob.links[i].used && s_blob.links[i].localPrefs != prefsIndex) others++;
  }
  // An empty save still takes one slot so the average stays NAN instead of
  // falling back to every non-outside sensor.
  const uint16_t need = (count == 0) ? 1 : count;
  if ((uint16_t)others + need > AGG_MAX_LINKS) {
    err = "full";
    return false;
  }
  for (uint8_t i = 0; i < AGG_MAX_LINKS; i++) {
    if (s_blob.links[i].used && s_blob.links[i].localPrefs == prefsIndex) s_blob.links[i].used = 0;
  }
  if (count == 0) {
    for (uint8_t s = 0; s < AGG_MAX_LINKS; s++) {
      if (s_blob.links[s].used) continue;
      s_blob.links[s].used = 1;
      s_blob.links[s].snsType = 0;
      s_blob.links[s].snsID = 0;
      s_blob.links[s].localPrefs = prefsIndex;
      s_blob.links[s].mac = 0;
      AggLinks_save();
      return true;
    }
    err = "full";
    return false;
  }
  uint8_t placed = 0;
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t s = 0; s < AGG_MAX_LINKS; s++) {
      if (s_blob.links[s].used) continue;
      s_blob.links[s].used = 1;
      s_blob.links[s].snsType = picks[i].snsType;
      s_blob.links[s].snsID = picks[i].snsID;
      s_blob.links[s].localPrefs = prefsIndex;
      s_blob.links[s].mac = picks[i].mac;
      placed++;
      break;
    }
  }
  if (placed != count) {
    err = "full";
    return false;
  }
  AggLinks_save();
  return true;
}

void AggLinks_load() {
  AggBlob incoming;
  if (!readBlob(incoming) || incoming.ver != AGG_VER) return;
  s_blob = incoming;
  s_loading = true;
  for (uint8_t i = 0; i < AGG_MAX_DEVICES; i++) {
    const AggDeviceRec& d = s_blob.devs[i];
    if (!d.used || d.mac == 0) continue;
    const IPAddress ip(d.ip[0], d.ip[1], d.ip[2], d.ip[3]);
    Sensors.addDevice(d.mac, ip, d.name, d.sendingInt, 0, d.devType);
  }
  s_loading = false;
}

#endif
