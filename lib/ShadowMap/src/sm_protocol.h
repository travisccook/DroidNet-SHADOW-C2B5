// The "@SM" line protocol DroidNet uses to read and edit the mapping.
//
//   request:  @SM <VERB> [args] [*<CRC32>]      (the CRC is required on the WCB port)
//   reply:    @SM <WORD> [args] *<CRC32>
//
// The CRC is CRC32 (IEEE, as zlib) of everything before " *", as 8 hex digits. Nothing in a
// line ever contains '^' (the WCB splits commands on it): prefix, suffix and raw commands travel
// hex-encoded (a raw command as "!" and its bytes). See WIPDocs/COMMAND_MAPPING_DESIGN.md.
#pragma once

#include <stdint.h>

#include "sm_battery.h"
#include "sm_mac.h"
#include "sm_out.h"
#include "sm_store.h"
#include "sm_tx.h"

namespace sm {

// Where replies to a request go. The protocol hands over the line without framing; the port
// adds whatever its transport needs.
class ReplyPort {
  public:
    virtual bool canSend(uint8_t len) = 0;
    virtual void send(const char *line, uint8_t len) = 0;
    virtual bool requiresCrc() const = 0;
    // HELLO's reply route. Only the WCB port uses it.
    virtual bool setRoute(uint8_t wcb) {
        (void)wcb;
        return true;
    }

  protected:
    ~ReplyPort() = default;
};

// Replies through the WCB. The WCB prints ";S0<text>" on its USB port, which is where the
// Pi listens; ";W<n>;S0<text>" does the same on WCB n. Route 0 means the Pi is plugged into
// SHADOW's own WCB.
class WcbPort final : public ReplyPort {
  public:
    static constexpr uint8_t kMaxRoute = 20;

    explicit WcbPort(TxQueue &tx) : tx_(tx) { buildRoute(); }
    bool canSend(uint8_t len) override;
    void send(const char *line, uint8_t len) override;
    bool requiresCrc() const override { return true; }
    bool setRoute(uint8_t wcb) override;
    uint8_t route() const { return route_; }
    const char *routePrefix() const { return routeText_; }

  private:
    void buildRoute();

    TxQueue &tx_;
    uint8_t route_ = 0;
    char routeText_[10] = {};  // longest is ";W20,;S0"
    uint8_t routeLen_ = 0;
};

class Protocol {
  public:
    // Longest reply, before port framing: a SLOT with a 15-character raw command ("!" and 30 hex
    // digits) and a 15-character default on F CIRCLE RIGHTL is 93 with its CRC.
    static constexpr uint8_t kMaxLine = kLineMax;  // 96
    // The shortest PRESS line: "@SM PRESS F L1 UP - eeprom *XXXXXXXX".
    static constexpr uint8_t kPressLineMin = 36;

    Protocol(MapStore &store, const char *fwVersion) : store_(store), fw_(fwVersion) {}

    // Handles one complete "@SM" line. Returns false, having done nothing, when it can't be
    // answered yet (the port is full, or the EEPROM is mid-write); call again later with
    // the same line.
    bool handleLine(const char *line, ReplyPort &port);

    // Finishes writes, sends their replies and paces DUMP. Call every loop pass.
    void service();

    // A combo reached the mapping (mapped or not). Sent to whichever port asked for EVENTS, if it
    // has room for this line; `modFromOther` says the modifier was held on the other controller,
    // `raw` that `key` is a raw command (sent as "!<hex>").
    void notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw = false);

    // Controller connection state, for READY and (with EVENTS on) "@SM CTL" events.
    void setPads(bool footConnected, bool domeConnected);

    // The USB port. "@SM REFUSED" goes there without EVENTS: it's where the owner looks for it
    // (DroidNet's Serial tab). Call once from setup().
    void setConsole(ReplyPort &port) { console_ = &port; }

    // A paired controller whose address isn't allowed tried to connect as `role`. `bdaddr` is
    // least significant byte first (Btd.disc_bdaddr). service() sends "@SM REFUSED <F|D> <address>"
    // to the console and to the event port when that's another port, each when it has room. A
    // newer refusal replaces one not yet sent.
    void noteRefused(Ctrl role, const uint8_t bdaddr[kMacLen]);

    // The controllers' battery levels, for "@SM BAT" (a BAT request, or sendBattery()).
    void setBattery(const BatteryReport &report) { battery_ = report; }

    // Sends "@SM BAT F=<lvl> D=<lvl> fs=<status> ds=<status> up=<s> hb=<s>" to `port`, or
    // nothing if the port has no room right now; returns whether it was sent.
    bool sendBattery(ReplyPort &port);

    bool opInProgress() const { return op_ != Op::None; }

  private:
    enum class Op : uint8_t { None, Set, Revert, Defaults, Prefix, Suffix, Dump, Sum, Mac, MacList };

    bool sendSlotLine(uint8_t slot, ReplyPort &port);
    bool handleMac(uint8_t argc, char *const *argv, ReplyPort &port);
    void sendMacLine(uint8_t index, ReplyPort &port);  // "@SM MAC <S> <address|-> <eeprom|default>"
    bool servicePads();     // whether it sent a line
    bool serviceRefused();  // the same
    void sendOpReply(bool ok);
    void startOp(Op op, ReplyPort &port, MapStore::Result r, const char *verb, uint8_t slot);

    MapStore &store_;
    const char *fw_;

    Op op_ = Op::None;
    ReplyPort *opPort_ = nullptr;
    uint8_t opSlot_ = kNoSlot;
    // The OK reply's echo: a key, a raw command as "!" + 30 hex digits, or PREFIX/SUFFIX's hex.
    static constexpr uint8_t kOpArgBuf = 2 + 2 * kKeyMax;
    static_assert(kOpArgBuf >= 2 * kPrefixMax + 1, "PREFIX's hex echo must fit opArg_");
    char opArg_[kOpArgBuf] = {};
    bool replyPending_ = false;
    bool opOk_ = false;
    uint8_t dumpNext_ = 0;  // next slot for DUMP / SUM
    uint32_t sumCrc_ = 0;
    uint8_t opMac_ = 0;  // MAC: the controller slot; MAC list: the next one

    ReplyPort *eventPort_ = nullptr;
    uint8_t pads_ = 0;          // bit 0 foot, bit 1 dome
    uint8_t padsReported_ = 0;  // what the event port last heard
    BatteryReport battery_ = {};

    ReplyPort *console_ = nullptr;
    uint8_t refusedMac_[kMacLen] = {};  // most significant byte first
    char refusedRole_ = 'F';
    bool refusedToConsole_ = false;
    bool refusedToEvents_ = false;
};

}  // namespace sm
