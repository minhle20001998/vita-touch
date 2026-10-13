#include "config.h"
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <string.h>

namespace config {

const char* dir() { return "ux0:data/VitaTouchPad"; }
const char* path() { return "ux0:data/VitaTouchPad/config.json"; }

bool parseIpv4(const std::string& ip, unsigned char out[4]) {
    if (ip.empty() || ip.size() > 15) return false;
    int part = 0;
    int value = -1; // -1 = no digits yet in current part
    int dots = 0;
    for (size_t i = 0; i <= ip.size(); i++) {
        char c = (i < ip.size()) ? ip[i] : '\0';
        if (c >= '0' && c <= '9') {
            if (value < 0) value = 0;
            value = value * 10 + (c - '0');
            if (value > 255) return false;
        } else if (c == '.' || c == '\0') {
            if (value < 0) return false; // empty part
            if (part >= 4) return false;
            out[part++] = (unsigned char)value;
            value = -1;
            if (c == '.') dots++;
        } else {
            return false;
        }
    }
    return part == 4 && dots == 3;
}

bool validIpv4(const std::string& ip) {
    unsigned char b[4];
    return parseIpv4(ip, b);
}

bool validPort(int port) { return port >= 1 && port <= 65535; }

// --- minimal JSON writer (escapes " and \ and control chars) ---

static void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    out += '"';
}

// --- minimal JSON reader: find "key" : value ---

static const char* skipWs(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

// Returns pointer just past the ':' of "key":, or NULL if not found.
static const char* findValue(const char* json, const char* end, const char* key) {
    char quoted[64];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    size_t qlen = strlen(quoted);
    const char* p = json;
    while (p + qlen < end) {
        if (memcmp(p, quoted, qlen) == 0) {
            const char* q = skipWs(p + qlen, end);
            if (q < end && *q == ':') return q + 1;
        }
        p++;
    }
    return NULL;
}

static bool parseJsonString(const char* p, const char* end, const char** next, std::string& out) {
    p = skipWs(p, end);
    if (p >= end || *p != '"') return false;
    p++;
    out.clear();
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
            p++;
            if (*p == '"' || *p == '\\' || *p == '/') out += *p;
            else if (*p == 'n') out += '\n';
            else if (*p == 't') out += '\t';
            else if (*p == 'r') out += '\r';
            else out += *p; // \uXXXX and others: pass through best-effort
            p++;
        } else {
            out += *p++;
        }
    }
    if (p >= end) return false;
    *next = p + 1;
    return true;
}

static bool getString(const char* json, const char* end, const char* key, std::string& out) {
    const char* p = findValue(json, end, key);
    if (!p) return false;
    const char* next = NULL;
    if (!parseJsonString(p, end, &next, out)) return false;
    return true;
}

static bool getDouble(const char* json, const char* end, const char* key, double& out) {
    const char* p = findValue(json, end, key);
    if (!p) return false;
    p = skipWs(p, end);
    if (p >= end) return false;
    char* stop = NULL;
    double v = strtod(p, &stop);
    if (stop == p) return false;
    out = v;
    return true;
}

static bool getBool(const char* json, const char* end, const char* key, bool& out) {
    const char* p = findValue(json, end, key);
    if (!p) return false;
    p = skipWs(p, end);
    if (p >= end) return false;
    if (strncmp(p, "true", 4) == 0) { out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

bool save(const Config& c) {
    std::string j = "{";
    j += "\"serverIp\":";
    appendEscaped(j, c.serverIp);
    j += ",\"serverPort\":" + std::to_string(c.serverPort);
    j += ",\"passcode\":";
    appendEscaped(j, c.passcode);
    char fbuf[64];
    snprintf(fbuf, sizeof(fbuf), ",\"sensitivity\":%.3f", c.sensitivity);
    j += fbuf;
    snprintf(fbuf, sizeof(fbuf), ",\"scrollSensitivity\":%.3f", c.scrollSensitivity);
    j += fbuf;
    j += ",\"batteryMode\":";
    appendEscaped(j, c.batteryMode);
    j += ",\"dimLevel\":" + std::to_string(c.dimLevel);
    j += std::string(",\"startInScrollMode\":") + (c.startInScrollMode ? "true" : "false");
    j += ",\"tapSlop\":" + std::to_string(c.tapSlop);
    j += ",\"holdMs\":" + std::to_string(c.holdMs);
    j += ",\"stickSpeed\":" + std::to_string(c.stickSpeed);
    j += std::string(",\"showEventLog\":") + (c.showEventLog ? "true" : "false");
    j += std::string(",\"measureMode\":") + (c.measureMode ? "true" : "false");
    j += ",\"swipeDist\":" + std::to_string(c.swipeDist);
    j += ",\"pinchSlop\":" + std::to_string(c.pinchSlop);
    j += ",\"landingHoldMs\":" + std::to_string(c.landingHoldMs);
    j += "}";

    // atomic write: temp file + rename (mirrors vita-reader)
    std::string tmp = std::string(path()) + ".tmp";
    SceUID fd = sceIoOpen(tmp.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return false;
    int w = (int)sceIoWrite(fd, j.data(), (SceSize)j.size());
    sceIoClose(fd);
    if (w != (int)j.size()) {
        sceIoRemove(tmp.c_str());
        return false;
    }
    if (sceIoRename(tmp.c_str(), path()) < 0) {
        // rename failed; fall back to direct overwrite so the value persists
        sceIoRemove(tmp.c_str());
        SceUID fd2 = sceIoOpen(path(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd2 < 0) return false;
        int w2 = (int)sceIoWrite(fd2, j.data(), (SceSize)j.size());
        sceIoClose(fd2);
        return w2 == (int)j.size();
    }
    return true;
}

bool load(Config& out) {
    out = Config();

    SceUID fd = sceIoOpen(path(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        sceIoClose(fd);
        return false;
    }
    std::string buf;
    buf.resize((size_t)size);
    int r = (int)sceIoRead(fd, &buf[0], (SceSize)size);
    sceIoClose(fd);
    if (r != (int)size) return false;

    const char* json = buf.data();
    const char* end = json + buf.size();
    std::string s;
    double d = 0;
    bool b = false;

    if (getString(json, end, "serverIp", s)) out.serverIp = s;
    if (getDouble(json, end, "serverPort", d)) out.serverPort = (int)d;
    if (getString(json, end, "passcode", s)) out.passcode = s;
    if (getDouble(json, end, "sensitivity", d)) out.sensitivity = (float)d;
    if (getDouble(json, end, "scrollSensitivity", d)) out.scrollSensitivity = (float)d;
    if (getString(json, end, "batteryMode", s)) out.batteryMode = s;
    if (getDouble(json, end, "dimLevel", d)) out.dimLevel = (int)d;
    if (getBool(json, end, "startInScrollMode", b)) out.startInScrollMode = b;
    if (getDouble(json, end, "tapSlop", d)) out.tapSlop = (int)d;
    if (getDouble(json, end, "holdMs", d)) out.holdMs = (int)d;
    if (getDouble(json, end, "stickSpeed", d)) out.stickSpeed = (int)d;
    {   // absent key keeps the Config() default (false)
        bool v = false;
        if (getBool(json, end, "showEventLog", v)) out.showEventLog = v;
    }
    {   // absent key keeps the Config() default (false)
        bool v = false;
        if (getBool(json, end, "measureMode", v)) out.measureMode = v;
    }
    if (getDouble(json, end, "swipeDist", d)) out.swipeDist = (int)d;
    if (getDouble(json, end, "pinchSlop", d)) out.pinchSlop = (int)d;
    if (getDouble(json, end, "landingHoldMs", d)) out.landingHoldMs = (int)d;

    // sanitize anything out of range (hand-edited or corrupt files)
    if (!validPort(out.serverPort)) out.serverPort = 31415;
    if (out.passcode.size() > 15) out.passcode.resize(15);
    if (out.sensitivity < 0.25f || out.sensitivity > 4.0f) out.sensitivity = 1.0f;
    if (out.scrollSensitivity < 0.25f || out.scrollSensitivity > 4.0f) out.scrollSensitivity = 1.5f;
    if (out.batteryMode != "off" && out.batteryMode != "dim") out.batteryMode = "dim";
    if (out.dimLevel < 21) out.dimLevel = 21;
    if (out.dimLevel > 65536) out.dimLevel = 65536;
    if (out.tapSlop < 4) out.tapSlop = 4;
    if (out.tapSlop > 64) out.tapSlop = 64;
    if (out.holdMs < 100) out.holdMs = 100;
    if (out.holdMs > 1000) out.holdMs = 1000;
    if (out.stickSpeed < 120) out.stickSpeed = 120;
    if (out.stickSpeed > 2400) out.stickSpeed = 2400;
    if (out.swipeDist < 40) out.swipeDist = 40;
    if (out.swipeDist > 400) out.swipeDist = 400;
    if (out.pinchSlop < 16) out.pinchSlop = 16;
    if (out.pinchSlop > 120) out.pinchSlop = 120;
    if (out.landingHoldMs < 0) out.landingHoldMs = 0;
    if (out.landingHoldMs > 200) out.landingHoldMs = 200;
    return true;
}

void seedDefault(Config& c) {
    if (c.serverPort == 0) c.serverPort = 31415;
    if (c.sensitivity <= 0) c.sensitivity = 1.0f;
    if (c.scrollSensitivity <= 0) c.scrollSensitivity = 1.5f;
    if (c.batteryMode != "off" && c.batteryMode != "dim") c.batteryMode = "dim";
    if (c.dimLevel < 21) c.dimLevel = 21;
    if (c.tapSlop <= 0) c.tapSlop = 12;
    if (c.holdMs <= 0) c.holdMs = 250;
    if (c.stickSpeed <= 0) c.stickSpeed = 600;
    if (c.swipeDist <= 0) c.swipeDist = 100;
    if (c.pinchSlop <= 0) c.pinchSlop = 40;
    if (c.landingHoldMs < 0) c.landingHoldMs = 0;
    // serverIp stays empty until the user configures it ("not configured" state)
}

} // namespace config
