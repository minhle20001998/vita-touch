// Fake time + fake UDP capture for the gesture engine host test.
#include <psp2/kernel/processmgr.h>
#include "udp.h"
#include <string>
#include <vector>

uint64_t g_fakeUs = 0;
uint32_t sceKernelGetProcessTimeLow() { return (uint32_t)g_fakeUs; }

static std::vector<std::string> g_pkts;

namespace udp {
bool init() { return true; }
void shutdown() {}
bool isUp() { return true; }
bool setDestination(const std::string&, int) { return true; }
bool hasDestination() { return true; }
bool sendRaw(const void* data, unsigned len) {
    const unsigned char* p = (const unsigned char*)data;
    static const char* hexd = "0123456789abcdef";
    std::string h;
    for (unsigned i = 0; i < len; i++) {
        h += hexd[p[i] >> 4];
        h += hexd[p[i] & 15];
    }
    g_pkts.push_back(h);
    return true;
}
bool sendHello(uint8_t, bool, const std::string&) { return true; }
bool sendMode(uint8_t, const std::string&) { return true; }
bool sendVPad(uint8_t, uint8_t, const std::string&) { return true; }
bool sendPing(const std::string&) { return true; }
Stats stats() { return Stats(); }
void resetStats() {}
} // namespace udp

void cap_reset() { g_pkts.clear(); }
size_t cap_count() { return g_pkts.size(); }
const std::string& cap_get(size_t i) { return g_pkts[i]; }
