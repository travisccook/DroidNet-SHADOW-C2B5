// SHADOW's mapping side on the host, for testing Pi-side code against the real firmware
// logic: the same ShadowMap library and built-in defaults, driven the way main.cpp drives
// them, with EEPROM kept in a file.
//
// stdin carries what SHADOW's Serial1 would receive from the WCB; stdout carries what SHADOW
// would write to Serial1. Lines starting with "!!" are simulator controls, not serial data:
//
//   !!PRESS <foot-mask> <dome-mask> [<foot-connected> <dome-connected>]
//                         hold these buttons (HeldBit masks, hex) for one press
//   !!TICK <ms>           let time pass with nothing held
//   !!BATT <foot> <dome>  the controllers' battery status bytes from now on (hex, 0 = none)
//   !!CUT <writes>        power fails after that many more EEPROM byte writes
//   !!REBOOT              restart SHADOW (EEPROM survives, RAM doesn't)
//
// After each input line the simulator runs loop passes until SHADOW has nothing left to do,
// then prints "!!IDLE <ms>".
//
// Build (from the repo root; test_shadowmap.py does this for you):
//   g++ -std=gnu++17 -I lib/ShadowMap/src -I include tools/shadowmap/shadow_sim.cpp
//       lib/ShadowMap/src/*.cpp src/default_mappings.cpp -o shadow_sim
#include <ShadowMap.h>
#include <default_mappings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr uint32_t kPassMs = 2;    // a loop pass
constexpr uint8_t kWriteTicks = 2;  // ~3.3 ms per EEPROM byte at 2 ms per pass

class FileEeprom final : public sm::EepromIo {
  public:
    explicit FileEeprom(std::string path) : path_(std::move(path)) {
        memset(mem_, 0xFF, sizeof(mem_));
        if (FILE *f = fopen(path_.c_str(), "rb")) {
            const size_t n = fread(mem_, 1, sizeof(mem_), f);
            (void)n;
            fclose(f);
        }
    }
    bool ready() override { return busy_ == 0; }
    uint8_t read(uint16_t addr) override { return addr < sizeof(mem_) ? mem_[addr] : 0xFF; }
    void write(uint16_t addr, uint8_t value) override {
        if (addr >= sizeof(mem_))
            return;
        if (budget_ == 0)
            return;  // no power
        if (budget_ > 0)
            --budget_;
        mem_[addr] = value;
        busy_ = kWriteTicks;
        save();
    }
    void tick() {
        if (busy_ > 0)
            --busy_;
    }
    void cutAfter(long writes) { budget_ = writes; }
    void powerOn() {
        busy_ = 0;
        budget_ = -1;
    }

  private:
    void save() {
        if (FILE *f = fopen(path_.c_str(), "wb")) {
            fwrite(mem_, 1, sizeof(mem_), f);
            fclose(f);
        }
    }

    std::string path_;
    uint8_t mem_[4096];
    uint8_t busy_ = 0;
    long budget_ = -1;
};

// SHADOW's Serial1 transmit side: 9600 baud is about 2 bytes per 2 ms pass.
struct StdoutUart {
    int room = 63;
    int availableForWrite() { return room; }
    size_t write(const uint8_t *data, size_t n) {
        for (size_t i = 0; i < n; ++i)
            putchar(data[i] == '\r' ? '\n' : data[i]);
        room -= static_cast<int>(n);
        return n;
    }
    void refill() { room = room + 2 > 63 ? 63 : room + 2; }
};

struct InputQueue {
    std::string bytes;
    size_t pos = 0;
    int available() { return pos < bytes.size() ? 1 : 0; }
    int read() { return pos < bytes.size() ? static_cast<unsigned char>(bytes[pos++]) : -1; }
    bool empty() const { return pos >= bytes.size(); }
};

uint32_t now = 0;
uint8_t footBattery = 0;
uint8_t domeBattery = 0;

struct Shadow {
    explicit Shadow(FileEeprom &eeprom)
        : store(eeprom, defaultButtonMappings, defaultButtonMappingsCount), port(tx), protocol(store, "sim"),
          runtime(store, protocol, tx, "sim") {
        store.begin();
        runtime.startAnnounce(now, 1);  // as at the end of main.cpp's setup()
        runtime.startBattery(port, now);
    }
    sm::MapStore store;
    sm::TxQueue tx;
    sm::WcbPort port;
    sm::Protocol protocol;
    sm::Runtime runtime;
    sm::LineReader reader;
};

void pass(Shadow &s, FileEeprom &eeprom, StdoutUart &uart, InputQueue &in, sm::PadState foot, sm::PadState dome) {
    s.runtime.pump(in, s.reader, s.port);
    sm::FiredPress fired;
    s.runtime.buttons(now, foot, dome, &fired);
    s.runtime.batteries(now, foot.connected, footBattery, dome.connected, domeBattery);
    s.runtime.service(now, uart);
    eeprom.tick();
    uart.refill();
    now += kPassMs;
}

bool busy(Shadow &s, InputQueue &in, const StdoutUart &uart) {
    return !in.empty() || s.reader.ready() || s.store.busy() || s.protocol.opInProgress() || s.tx.used() > 0 ||
           uart.room < 63;  // still clocking bytes out of the UART
}

}  // namespace

int main(int argc, char **argv) {
    const char *eepromPath = argc > 1 ? argv[1] : "shadow_sim.eeprom";
    FileEeprom eeprom(eepromPath);
    StdoutUart uart;
    InputQueue in;
    auto shadow = std::make_unique<Shadow>(eeprom);
    sm::PadState foot = {true, 0};
    sm::PadState dome = {true, 0};

    char buf[512];
    while (fgets(buf, sizeof(buf), stdin) != nullptr) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();

        if (line.rfind("!!", 0) == 0) {
            unsigned footMask = 0, domeMask = 0;
            int footOn = 1, domeOn = 1;
            long n = 0;
            if (sscanf(line.c_str(), "!!PRESS %x %x %d %d", &footMask, &domeMask, &footOn, &domeOn) >= 2) {
                foot = {footOn != 0, static_cast<uint16_t>(footMask)};
                dome = {domeOn != 0, static_cast<uint16_t>(domeMask)};
                pass(*shadow, eeprom, uart, in, foot, dome);
                foot.held = 0;
                dome.held = 0;
                now += sm::Runtime::kRepeatMs + 1;  // so the next press isn't inside the repeat window
            } else if (sscanf(line.c_str(), "!!TICK %ld", &n) == 1) {
                for (long t = 0; t < n; t += kPassMs)
                    pass(*shadow, eeprom, uart, in, foot, dome);
            } else if (sscanf(line.c_str(), "!!BATT %x %x", &footMask, &domeMask) == 2) {
                footBattery = static_cast<uint8_t>(footMask);
                domeBattery = static_cast<uint8_t>(domeMask);
                pass(*shadow, eeprom, uart, in, foot, dome);  // SHADOW reads the controllers every pass
            } else if (sscanf(line.c_str(), "!!CUT %ld", &n) == 1) {
                eeprom.cutAfter(n);
            } else if (line == "!!REBOOT") {
                eeprom.powerOn();
                shadow = std::make_unique<Shadow>(eeprom);
                in = InputQueue();
                uart.room = 63;
            } else {
                fprintf(stderr, "unknown control: %s\n", line.c_str());
                return 2;
            }
        } else {
            in.bytes.append(line);
            in.bytes.push_back('\r');  // the WCB ends every line it writes to an S-port with \r
        }

        for (int i = 0; i < 20000 && busy(*shadow, in, uart); ++i)
            pass(*shadow, eeprom, uart, in, foot, dome);
        printf("!!IDLE %u\n", static_cast<unsigned>(now));
        fflush(stdout);
    }
    return 0;
}
