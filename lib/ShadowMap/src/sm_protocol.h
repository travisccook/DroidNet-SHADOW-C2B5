// The "@SM" line protocol DroidNet uses to read and edit the mapping.
//
//   request:  @SM <VERB> [args] [*<CRC32>]      (the CRC is required on the WCB port)
//   reply:    @SM <WORD> [args] *<CRC32>
//
// The CRC is CRC32 (IEEE, as zlib) of everything before " *", as 8 hex digits. Nothing in a
// line ever contains '^' (the WCB splits commands on it). See WIPDocs/COMMAND_MAPPING_DESIGN.md.
#pragma once

#include <stdint.h>

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
};

class Protocol {
  public:
    static constexpr uint8_t kMaxLine = 80;  // longest reply, before port framing

    Protocol(MapStore &store, const char *fwVersion) : store_(store), fw_(fwVersion) {}

    // Handles one complete "@SM" line. Returns false, having done nothing, when it can't be
    // answered yet (the port is full, or the EEPROM is mid-write); call again later with
    // the same line.
    bool handleLine(const char *line, ReplyPort &port);

    // Finishes writes, sends their replies and paces DUMP. Call every loop pass.
    void service();

    // A mapped combo was pressed. Sent to whichever port asked for EVENTS, if there's room.
    void notifyPress(uint8_t slot, const char *key, Source src);

    bool opInProgress() const { return op_ != Op::None; }

  private:
    enum class Op : uint8_t { None, Set, Defaults, Prefix, Suffix, Dump };

    bool sendSlotLine(uint8_t slot, ReplyPort &port);
    void sendOpReply(bool ok);
    void startOp(Op op, ReplyPort &port, MapStore::Result r, const char *verb, uint8_t slot);

    MapStore &store_;
    const char *fw_;

    Op op_ = Op::None;
    ReplyPort *opPort_ = nullptr;
    uint8_t opSlot_ = kNoSlot;
    char opArg_[2 * kPrefixMax + 1] = {};  // key or hex echo for the OK reply
    bool replyPending_ = false;
    bool opOk_ = false;
    uint8_t dumpNext_ = 0;

    ReplyPort *eventPort_ = nullptr;
};

}  // namespace sm
