// Button-mashing simulation of SHADOW's Serial1 link to WCB 1. Run: pio test -e native
//
// Simulated minutes of button presses, far faster than SHADOW can send them, through
// models of both ends of the wire:
//   * SHADOW: the AVR core's HardwareSerial TX path (framework-arduino-avr 5.2.0): a
//     64-byte ring, availableForWrite() = 63 - queued, write() spinning while the ring
//     is full, 9600 8N1. It is fed the real command strings from src/main.cpp and the
//     real wdp::AnnounceTimer, driven by the same tick as wdpAnnounceTick().
//   * WCB 1: the line splitter from WCB.ino processIncomingSerial(). A line ends at CR
//     or LF and is trimmed. If it starts with "@WDP" it goes to the WDP-DA handler;
//     otherwise it goes to the command parser.
// This replaces sitting at the droid pressing buttons across announce windows. It does
// not cover electrical faults on the real wire.
//
// Never build this for the Mega (see test_wdp_announce for why).
#ifdef ARDUINO
#error "Host-only test: run pio test -e native"
#endif

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "../../lib/WdpAnnounce/WdpAnnounce.h"

namespace {

constexpr uint64_t kByteUs = 1042;  // one 10-bit frame at 9600 baud, rounded up
constexpr size_t kRingSize = 64;    // SERIAL_TX_BUFFER_SIZE
constexpr uint64_t kMinute = 60ULL * 1000 * 1000;
constexpr uint64_t kWcbTtlUs = 90ULL * 1000 * 1000;  // WCB forgets a silent device after 90 s

// The announce as compiled: "@WDP1 {\"type\":\"Shadow\",\"fw\":\"" GIT_REV "\"}\r".
// Uses the longest fw the static_assert allows (14 characters), the worst case for delay.
const std::string kAnnounce = "@WDP1 {\"type\":\"Shadow\",\"fw\":\"e88b489-dirty\"}\r";

// What one button press writes to Serial1: one print() per element, taken from
// marcDuinoButtonPush() in src/main.cpp. Multi-line presses are listed together.
const std::vector<std::vector<std::string>> kPresses = {
    {";W1;Cscream\r"},
    {";W1;Cshortcircuit\r"},
    {";W1;Cleia\r"},
    {";W1;Cdisco\r"},
    {"<PVV90,PVA90,PVB90>\r", "R0053|1\r"},  // volume up
    {"<PVV80,PVA80,PVB80>\r", "R0051|1\r"},  // volume down
    {"R0051|5\r"},                           // volume already at a limit
    {"<PVV100,PVA100,PVB100>\r", "R0058|1\r"},
    {"<PVV10,PVA10,PVB10>\r", "R0059|2\r"},
    {";W1;Cmagicpanelflicker\r"},
    {";W1;Cresetchatty\r"},
    {";W1;Cimperialmarch\r"},
    {";W1;Cfrontholocenter\r"},
    {";W1;Cjeditheme\r"},
    {"1P1\r", "2P150\r"},  // brightness down / up
    {"1P1\r", "2P200\r"},  // brightness max
    {";W1;Cextremesadsound\r"},
    {";W1;Cextremehappysou\r"},
    {";W1;CGDrecruitsong\r"},
    {";W1;Credlight\r"},
};

enum class Src : char { Command = 'C', Announce = 'A' };

struct WireByte {
    char c;
    Src src;
    uint64_t queuedUs;  // when print() handed it to write()
    uint64_t sentUs;    // when its start bit went out
};

// HardwareSerial TX. write() writes straight to the data register when the line is idle
// and the ring is empty. Otherwise the byte waits in the ring, and write() spins while the
// ring is full. Bytes leave in order, one frame time apart.
class Serial1Model {
public:
    explicit Serial1Model(uint64_t &clockUs) : clock_(clockUs) {}

    int availableForWrite() {
        drain();
        return static_cast<int>(kRingSize - 1 - ring_.size());
    }

    // Serial1.print(): write() each byte. Returns how long the caller was blocked.
    uint64_t print(const std::string &s, Src src) {
        uint64_t blocked = 0;
        for (char c : s) {
            drain();
            while (ring_.size() == kRingSize - 1) {  // full: spin until the ISR frees a slot
                blocked += busyUntil_ - clock_;
                clock_ = busyUntil_;
                drain();
            }
            const WireByte b{c, src, clock_, 0};
            if (ring_.empty() && busyUntil_ <= clock_) {
                send(b, clock_);
            } else {
                ring_.push_back(b);
            }
        }
        return blocked;
    }

    void drain() {
        while (!ring_.empty() && busyUntil_ <= clock_) {
            const WireByte b = ring_.front();
            ring_.pop_front();
            send(b, busyUntil_);
        }
    }

    const std::vector<WireByte> &wire() const { return wire_; }

private:
    void send(WireByte b, uint64_t at) {
        b.sentUs = at;
        wire_.push_back(b);
        busyUntil_ = at + kByteUs;
    }

    uint64_t &clock_;
    std::deque<WireByte> ring_;
    uint64_t busyUntil_ = 0;
    std::vector<WireByte> wire_;
};

std::string trim(const std::string &s) {  // Arduino String::trim(): isspace() at both ends
    const char *ws = " \t\r\n\f\v";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// WCB.ino processIncomingSerial(): split on CR/LF, skip empty lines, trim, route.
struct WcbLines {
    std::vector<std::string> announces;
    std::vector<std::string> commands;
};

WcbLines wcbSplit(const std::vector<WireByte> &wire) {
    WcbLines out;
    std::string buf;
    for (const WireByte &b : wire) {
        if (b.c == '\r' || b.c == '\n') {
            if (!buf.empty()) {
                const std::string line = trim(buf);
                if (line.rfind("@WDP", 0) == 0) {
                    out.announces.push_back(line);
                } else {
                    out.commands.push_back(line);
                }
                buf.clear();
            }
        } else {
            buf += b.c;
        }
    }
    return out;
}

struct SimResult {
    std::vector<std::string> sentCommands;  // trimmed, in print order
    size_t announcesSent = 0;
    uint64_t announceBlockedUs = 0;   // time loop() spent blocked inside an announce print()
    uint64_t maxAnnounceHoldUs = 0;   // longest a command's first byte waited behind announce bytes
    uint64_t maxAnnounceGapUs = 0;    // longest gap between announces (and to the end of the run)
    WcbLines received;
};

// One run. meanPressGapUs sets how hard the buttons are mashed. Each loop() pass costs
// loopMinUs..loopMaxUs (Usb.Task, drive, dome, the button tables).
SimResult simulate(uint32_t seed, uint64_t durationUs, uint64_t meanPressGapUs, uint64_t loopMinUs,
                   uint64_t loopMaxUs) {
    std::mt19937 rng(seed);
    std::exponential_distribution<double> pressGap(1.0 / static_cast<double>(meanPressGapUs));
    std::uniform_int_distribution<uint64_t> loopCost(loopMinUs, loopMaxUs);
    std::uniform_int_distribution<size_t> pickPress(0, kPresses.size() - 1);
    // main.cpp calls Arduino random(min, max + 1), which returns min..max.
    std::uniform_int_distribution<uint32_t> firstDelay(wdp::kFirstDelayMinMs, wdp::kFirstDelayMaxMs);
    std::uniform_int_distribution<uint32_t> interval(wdp::kIntervalMinMs, wdp::kIntervalMaxMs);

    uint64_t clock = 0;
    Serial1Model serial1(clock);
    wdp::AnnounceTimer timer;
    SimResult r;
    std::vector<uint64_t> announceAtUs;
    auto millis = [&clock]() { return static_cast<uint32_t>(clock / 1000); };

    timer.start(millis(), firstDelay(rng));  // end of setup()
    uint64_t nextPressUs = static_cast<uint64_t>(pressGap(rng));

    while (clock < durationUs) {
        clock += loopCost(rng);

        // simpleMarcDuinoDome()/Foot(): at most one press handled per pass
        if (clock >= nextPressUs) {
            for (const std::string &line : kPresses[pickPress(rng)]) {
                serial1.print(line, Src::Command);
                r.sentCommands.push_back(trim(line));
            }
            nextPressUs = clock + static_cast<uint64_t>(pressGap(rng));
        }

        // wdpAnnounceTick(), as in src/main.cpp
        const size_t lineLen = kAnnounce.size();
        if (timer.ready(millis(), serial1.availableForWrite(), lineLen)) {
            announceAtUs.push_back(clock);
            r.announceBlockedUs += serial1.print(kAnnounce, Src::Announce);
            r.announcesSent++;
            timer.markSent(millis(), interval(rng));
        }
    }

    clock += 200 * kByteUs;  // let the wire finish
    serial1.drain();
    const std::vector<WireByte> &wire = serial1.wire();
    r.received = wcbSplit(wire);

    // For the first byte of each command line: how many announce bytes went out between
    // its print() and its own start bit?
    for (size_t i = 0; i < wire.size(); ++i) {
        const bool lineStart = i == 0 || wire[i - 1].c == '\r';
        if (wire[i].src != Src::Command || !lineStart) continue;
        uint64_t announceBytes = 0;
        for (size_t j = i; j-- > 0 && wire[j].sentUs >= wire[i].queuedUs;) {
            if (wire[j].src == Src::Announce) announceBytes++;
        }
        // The byte on the wire when the command was queued may be part-sent; count it whole.
        const uint64_t hold = announceBytes * kByteUs;
        if (hold > r.maxAnnounceHoldUs) r.maxAnnounceHoldUs = hold;
    }

    uint64_t prev = 0;
    announceAtUs.push_back(durationUs);
    for (uint64_t t : announceAtUs) {
        if (t - prev > r.maxAnnounceGapUs) r.maxAnnounceGapUs = t - prev;
        prev = t;
    }
    return r;
}

constexpr uint32_t kSeeds[] = {1, 2, 3, 4, 5, 6, 7, 8};

// Presses every ~150 ms on average: about 7x the one command per second SHADOW's
// button counter allows. Loop passes of 0.3-3 ms.
SimResult mash(uint32_t seed) {
    return simulate(seed, 10 * kMinute, 150000, 300, 3000);
}

void assertDelivered(const SimResult &r) {
    TEST_ASSERT_EQUAL_size_t(r.sentCommands.size(), r.received.commands.size());
    for (size_t i = 0; i < r.sentCommands.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(r.sentCommands[i].c_str(), r.received.commands[i].c_str());
    }
    TEST_ASSERT_EQUAL_size_t(r.announcesSent, r.received.announces.size());
    const std::string expected = trim(kAnnounce);
    for (const std::string &a : r.received.announces) {
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), a.c_str());
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_every_command_and_announce_arrives_whole(void) {
    for (uint32_t seed : kSeeds) {
        const SimResult r = mash(seed);
        TEST_ASSERT_GREATER_THAN_size_t(3000, r.sentCommands.size());  // the mashing really happened
        assertDelivered(r);
    }
}

void test_announce_never_blocks_loop(void) {
    for (uint32_t seed : kSeeds) {
        TEST_ASSERT_EQUAL_UINT64(0, mash(seed).announceBlockedUs);
    }
}

void test_announce_holds_a_command_for_at_most_one_announce(void) {
    uint64_t worst = 0;
    for (uint32_t seed : kSeeds) {
        const uint64_t hold = mash(seed).maxAnnounceHoldUs;
        if (hold > worst) worst = hold;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "worst command delay caused by an announce: %.1f ms", worst / 1000.0);
    TEST_MESSAGE(msg);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(kAnnounce.size() * kByteUs, worst);  // 46 bytes = 47.9 ms
}

void test_announces_keep_the_wcb_record_alive_while_mashing(void) {
    for (uint32_t seed : kSeeds) {
        const SimResult r = mash(seed);
        TEST_ASSERT_LESS_THAN_UINT64(kWcbTtlUs, r.maxAnnounceGapUs);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64(31ULL * 1000 * 1000, r.maxAnnounceGapUs);  // 30 s + one busy wait
    }
}

void test_normal_use_is_unaffected(void) {
    // One press every ~3 s: what a person actually does.
    for (uint32_t seed : kSeeds) {
        const SimResult r = simulate(seed, 10 * kMinute, 3000000, 300, 3000);
        assertDelivered(r);
        TEST_ASSERT_EQUAL_UINT64(0, r.announceBlockedUs);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64(kAnnounce.size() * kByteUs, r.maxAnnounceHoldUs);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_command_and_announce_arrives_whole);
    RUN_TEST(test_announce_never_blocks_loop);
    RUN_TEST(test_announce_holds_a_command_for_at_most_one_announce);
    RUN_TEST(test_announces_keep_the_wcb_record_alive_while_mashing);
    RUN_TEST(test_normal_use_is_unaffected);
    return UNITY_END();
}
