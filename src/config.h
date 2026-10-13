#pragma once
#include <string>

struct Config {
    std::string serverIp;        // empty = not configured
    int serverPort = 31415;
    std::string passcode;        // max 15 chars (header field is 16 incl. NUL)
    float sensitivity = 1.0f;
    float scrollSensitivity = 1.5f;
    std::string batteryMode = "dim"; // "dim" | "off"
    int dimLevel = 21;           // floor is 21; 0 is handled via batteryMode="off"
    bool startInScrollMode = false;
    int tapSlop = 12;            // tap/drag threshold, panel units (Phase 3 tuning)
    int holdMs = 250;            // tap-hold duration starting a drag, ms
    int stickSpeed = 600;        // right-stick cursor speed, panel units/sec
    bool showEventLog = false;   // on-screen event feed (debug); enable in Settings
    bool measureMode = false;    // record raw touch frames to measure_N.csv
    int swipeDist = 100;         // 3-finger swipe min distance, panel units
    int pinchSlop = 40;          // pinch/spread lock threshold, panel units
    int landingHoldMs = 80;      // fresh-touchdown motion hold (anti-teleport)
};

namespace config {
    const char* dir();
    const char* path();
    bool load(Config& out);
    bool save(const Config& c);
    void seedDefault(Config& c);
    // Parses a dotted-quad IPv4 address into 4 bytes. False on any bad input.
    bool parseIpv4(const std::string& ip, unsigned char out[4]);
    bool validIpv4(const std::string& ip);
    bool validPort(int port);
}
