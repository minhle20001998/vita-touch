#pragma once
#include <string>

namespace udp {
    // Loads the NET sysmodule, inits the net stack, creates the UDP socket.
    bool init();
    void shutdown();
    bool isUp();

    // Resolves ip/port into the send destination. Returns false on bad input
    // (destination left unchanged).
    bool setDestination(const std::string& ip, int port);
    bool hasDestination();

    // Sends one datagram. False when not initialized, unconfigured, or the
    // stack rejected the send. Updates stats either way.
    bool sendRaw(const void* data, unsigned len);

    // Typed protocol senders (passcode embedded per packet).
    bool sendHello(uint8_t mode, bool startInScroll, const std::string& passcode);
    bool sendMode(uint8_t mode, const std::string& passcode);
    bool sendVPad(uint8_t button, uint8_t action, const std::string& passcode);
    bool sendPing(const std::string& passcode);

    struct Stats {
        unsigned sent = 0;
        unsigned failed = 0;
        unsigned consecFails = 0;
    };
    Stats stats();
    void resetStats();
}
