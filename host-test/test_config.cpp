// Host test for src/config.cpp save/load/sanitize using the fake filesystem.
#include <stdio.h>
#include <string>
#include "config.h"

void fakefs_reset();
void fakefs_failNextRename();
std::string fakefs_read(const char* path);
void fakefs_write(const char* path, const std::string& data);

static int fails = 0;
static void check(bool cond, const char* msg) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) fails++;
}

int main() {
    // 1. missing file -> load false, seed fills defaults
    fakefs_reset();
    Config c;
    check(!config::load(c), "load missing file returns false");
    config::seedDefault(c);
    check(c.serverPort == 31415, "seeded port 31415");
    check(c.serverIp.empty(), "seeded ip empty");
    check(c.batteryMode == "dim", "seeded battery dim");
    check(c.tapSlop == 12, "seeded tapSlop 12");
    check(c.holdMs == 250, "seeded holdMs 250");
    check(c.stickSpeed == 600, "seeded stickSpeed 600");
    check(c.showEventLog == false, "seeded showEventLog false");
    check(c.measureMode == false, "seeded measureMode false");
    check(c.landingHoldMs == 80, "seeded landingHoldMs 80");

    // 2. full round trip
    c.serverIp = "192.168.1.100";
    c.serverPort = 31415;
    c.passcode = "s3cret!";
    c.sensitivity = 1.75f;
    c.scrollSensitivity = 2.5f;
    c.batteryMode = "off";
    c.dimLevel = 100;
    c.startInScrollMode = true;
    c.tapSlop = 20;
    c.holdMs = 400;
    c.stickSpeed = 900;
    c.landingHoldMs = 120;
    c.showEventLog = true;
    c.measureMode = true;
    check(config::save(c), "save returns true");
    std::string raw = fakefs_read(config::path());
    check(!raw.empty(), "file written to config path");
    check(fakefs_read((std::string(config::path()) + ".tmp").c_str()).empty(),
          "no .tmp left behind after rename");

    Config d;
    check(config::load(d), "load returns true");
    check(d.serverIp == "192.168.1.100", "roundtrip ip");
    check(d.serverPort == 31415, "roundtrip port");
    check(d.passcode == "s3cret!", "roundtrip passcode");
    check(d.sensitivity > 1.74f && d.sensitivity < 1.76f, "roundtrip sensitivity");
    check(d.scrollSensitivity > 2.49f && d.scrollSensitivity < 2.51f, "roundtrip scroll sens");
    check(d.batteryMode == "off", "roundtrip battery");
    check(d.dimLevel == 100, "roundtrip dimLevel");
    check(d.startInScrollMode == true, "roundtrip startInScroll");
    check(d.tapSlop == 20, "roundtrip tapSlop");
    check(d.holdMs == 400, "roundtrip holdMs");
    check(d.stickSpeed == 900, "roundtrip stickSpeed");
    check(d.landingHoldMs == 120, "roundtrip landingHoldMs");
    check(d.showEventLog == true, "roundtrip showEventLog true");
    check(d.measureMode == true, "roundtrip measureMode true");

    // 2b. absent showEventLog key keeps default false (old config files)
    fakefs_write(config::path(), "{\"serverIp\":\"1.2.3.4\"}");
    check(config::load(d) && d.showEventLog == false, "absent showEventLog defaults false");
    check(d.measureMode == false, "absent measureMode defaults false");

    // 3. special chars in strings survive (quotes/backslash)
    c.passcode = "a\"b\\c";
    check(config::save(c), "save special chars");
    check(config::load(d), "load special chars");
    check(d.passcode == "a\"b\\c", "special chars roundtrip");

    // 4. rename failure -> fallback direct write still persists
    fakefs_reset();
    fakefs_failNextRename();
    c = Config();
    c.serverIp = "10.0.0.5";
    check(config::save(c), "save with failing rename returns true");
    check(config::load(d), "load after fallback write");
    check(d.serverIp == "10.0.0.5", "fallback write persisted");

    // 5. corrupt/hand-edited file -> loads with sanitized values, no crash
    fakefs_reset();
    fakefs_write(config::path(), "{not valid json,,,");
    check(config::load(d), "corrupt file still loads defaults");
    check(d.serverPort == 31415, "corrupt file sanitized port");
    fakefs_write(config::path(),
                 "{\"serverPort\":99999,\"sensitivity\":99,\"batteryMode\":\"x\","
                 "\"passcode\":\"0123456789abcdef-too-long\",\"dimLevel\":5,"
                 "\"tapSlop\":1,\"holdMs\":5,\"stickSpeed\":5,\"landingHoldMs\":999}");
    check(config::load(d), "bad-values file loads");
    check(d.serverPort == 31415, "bad port sanitized");
    check(d.sensitivity == 1.0f, "bad sensitivity sanitized");
    check(d.batteryMode == "dim", "bad battery sanitized");
    check(d.passcode.size() <= 15, "long passcode truncated");
    check(d.dimLevel == 21, "low dimLevel floored");
    check(d.tapSlop == 4, "low tapSlop floored");
    check(d.holdMs == 100, "low holdMs floored");
    check(d.stickSpeed == 120, "low stickSpeed floored");
    check(d.landingHoldMs == 200, "high landingHoldMs capped");

    // 6. unknown future keys are ignored
    fakefs_write(config::path(), "{\"serverIp\":\"1.2.3.4\",\"newField\":123}");
    check(config::load(d) && d.serverIp == "1.2.3.4", "unknown keys ignored");

    // 7. IPv4 validation
    unsigned char b[4];
    check(config::parseIpv4("192.168.1.100", b) && b[0] == 192 && b[1] == 168 &&
              b[2] == 1 && b[3] == 100, "ipv4 parse octets");
    check(config::parseIpv4("0.0.0.0", b), "ipv4 0.0.0.0 parses");
    check(config::parseIpv4("255.255.255.255", b), "ipv4 broadcast parses");
    check(!config::validIpv4(""), "empty ip invalid");
    check(!config::validIpv4("192.168.1"), "short ip invalid");
    check(!config::validIpv4("192.168.1.1.1"), "long ip invalid");
    check(!config::validIpv4("192.168.1.256"), "octet >255 invalid");
    check(!config::validIpv4("192.168.1.-1"), "negative invalid");
    check(!config::validIpv4("192.168.1.a"), "alpha invalid");
    check(!config::validIpv4("192.168..1"), "empty part invalid");
    check(!config::validIpv4(".192.168.1.1"), "leading dot invalid");
    check(!config::validIpv4("192.168.1.1 "), "trailing space invalid");
    check(!config::validIpv4(" 192.168.1.1"), "leading space invalid");
    check(!config::validPort(0) && !config::validPort(65536), "port range enforced");
    check(config::validPort(1) && config::validPort(65535), "port edges valid");

    printf(fails ? "FAILURES: %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
