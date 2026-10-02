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
    // answered yet (the port is full, the EEPROM is mid-write, or a SLOT reply is still in hand
    // for this port, or for any port if this is a GET); call again later with the same line.
    // A GET reads its slot here and sends the line from serviceLine() on a later pass.
    bool handleLine(const char *line, ReplyPort &port);

    // Whether handleLine() would hold `line`, found without looking at it further, so
    // Runtime::pump can leave it for a later pass without counting it as a request taken in
    // (which would keep a SLOT reply in hand from going out):
    //   Take      no reason to wait
    //   Wait      a SLOT reply is in hand for this port (or, for a GET, for any port) and has room:
    //             it goes out on the next light pass; the bytes behind the line can stay in the UART
    //   WaitDrain no room on `port`, or the reply in hand has none: the wait may be long, so the
    //             bytes behind it are read and dropped, as for any held request
    enum class Hold : uint8_t { Take, Wait, WaitDrain };
    Hold holdFor(const char *line, ReplyPort &port);

    // The protocol's background work, one unit a call, in this order: a controller event, the
    // SLOT reply in hand (if its port has room), then the operation's next step (the SUM reply,
    // END DUMP, a DUMP slot read, or a write's reply). Returns true if it queued a line or read
    // a slot. A SLOT reply waiting for room doesn't hold up the rest.
    bool serviceLine();
    // One SUM step (a slot read and its CRC); false while there is none or the EEPROM is busy.
    bool serviceSum();
    // serviceLine(), or else serviceSum(): one unit. Runtime::service calls the two itself.
    bool service() { return serviceLine() || serviceSum(); }

    // A combo reached the mapping (mapped or not). Sent to whichever port asked for EVENTS, if it
    // has room for this line; `modFromOther` says the modifier was held on the other controller,
    // `raw` that `key` is a raw command (sent as "!<hex>").
    void notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw = false);

    // Controller connection state, for READY and (with EVENTS on) "@SM CTL" events.
    void setPads(bool footConnected, bool domeConnected);

    // The controllers' battery levels, for "@SM BAT" (a BAT request, or sendBattery()).
    void setBattery(const BatteryReport &report) { battery_ = report; }

    // Sends "@SM BAT F=<lvl> D=<lvl> fs=<status> ds=<status> up=<s> hb=<s>" to `port`, or
    // nothing if the port has no room right now; returns whether it was sent.
    bool sendBattery(ReplyPort &port);

    // An operation, or a SLOT reply read but not yet sent: keep calling service().
    bool opInProgress() const { return op_ != Op::None || slotOut_ != kNoSlot; }

  private:
    enum class Op : uint8_t { None, Set, Revert, Defaults, Prefix, Suffix, Dump, Sum };

    bool readSlot(uint8_t slot, ReplyPort &port);
    bool sendSlotReply();
    bool servicePads();
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

    // A SLOT reply (GET or a DUMP line) is read on one loop pass (the slot's record and its
    // CRC) and formatted and sent on a later one, so neither pass does both.
    uint8_t slotOut_ = kNoSlot;
    ReplyPort *slotOutPort_ = nullptr;
    char slotOutKey_[kKeyBuf] = {};
    Source slotOutSrc_ = Source::Default;
    bool slotOutRaw_ = false;

    ReplyPort *eventPort_ = nullptr;
    uint8_t pads_ = 0;          // bit 0 foot, bit 1 dome
    uint8_t padsReported_ = 0;  // what the event port last heard
    BatteryReport battery_ = {};
};

}  // namespace sm
