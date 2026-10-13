#include "udp.h"
#include "config.h"
#include "protocol.h"
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/kernel/processmgr.h>
#include <malloc.h>
#include <string.h>

namespace udp {

namespace {
bool g_up = false;
int g_sock = -1;
SceNetSockaddrIn g_dest;
bool g_hasDest = false;
uint32_t g_seq = 0;
Stats g_stats;
void* g_pool = NULL;

uint32_t nowMs() { return sceKernelGetProcessTimeLow() / 1000; }
} // namespace

bool init() {
    if (g_up) return true;
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

    const unsigned poolSize = 1024 * 1024; // UDP-only; no HTTP/TLS overhead
    g_pool = memalign(8, poolSize);
    if (!g_pool) return false;
    SceNetInitParam param;
    param.memory = g_pool;
    param.size = poolSize;
    param.flags = 0;
    if (sceNetInit(&param) < 0) return false;

    g_sock = sceNetSocket("VTPad", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (g_sock < 0) return false;
    g_up = true;
    return true;
}

void shutdown() {
    if (g_sock >= 0) {
        sceNetSocketClose(g_sock);
        g_sock = -1;
    }
    if (g_up) {
        sceNetTerm();
        g_up = false;
    }
    if (g_pool) {
        free(g_pool);
        g_pool = NULL;
    }
    g_hasDest = false;
}

bool isUp() { return g_up; }

bool setDestination(const std::string& ip, int port) {
    unsigned char bytes[4];
    if (!config::parseIpv4(ip, bytes)) return false;
    if (!config::validPort(port)) return false;
    memset(&g_dest, 0, sizeof(g_dest));
    g_dest.sin_len = sizeof(g_dest);
    g_dest.sin_family = SCE_NET_AF_INET;
    g_dest.sin_port = sceNetHtons((unsigned short)port);
    // s_addr is network byte order; memcpy of the parsed octets is endian-proof
    memcpy(&g_dest.sin_addr, bytes, sizeof(bytes));
    g_hasDest = true;
    return true;
}

bool hasDestination() { return g_hasDest; }

bool sendRaw(const void* data, unsigned len) {
    if (!g_up || !g_hasDest || !data || len == 0) return false;
    int rc = sceNetSendto(g_sock, data, len, 0,
                          (const SceNetSockaddr*)&g_dest, sizeof(g_dest));
    if (rc < 0) {
        g_stats.failed++;
        g_stats.consecFails++;
        return false;
    }
    g_stats.sent++;
    g_stats.consecFails = 0;
    return true;
}

bool sendHello(uint8_t mode, bool startInScroll, const std::string& passcode) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildHello(buf, mode, startInScroll, g_seq++,
                               nowMs(), passcode.c_str());
    return sendRaw(buf, (unsigned)n);
}

bool sendMode(uint8_t mode, const std::string& passcode) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildMode(buf, mode, g_seq++, nowMs(), passcode.c_str());
    return sendRaw(buf, (unsigned)n);
}

bool sendVPad(uint8_t button, uint8_t action, const std::string& passcode) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildVPad(buf, button, action, g_seq++, nowMs(), passcode.c_str());
    return sendRaw(buf, (unsigned)n);
}

bool sendPing(const std::string& passcode) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildPing(buf, g_seq++, nowMs(), passcode.c_str());
    return sendRaw(buf, (unsigned)n);
}

Stats stats() { return g_stats; }
void resetStats() { g_stats = Stats(); }

} // namespace udp
