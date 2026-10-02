#include "sm_protocol.h"

#include <string.h>

#include "sm_crc.h"
#include "sm_line.h"
#include "sm_out.h"
#include "sm_pgm.h"

namespace sm {

namespace {

constexpr uint8_t kProtocolVersion = 4;  // 2: double-tap slots; 3: long-press slots; 4: raw commands
constexpr uint8_t kMaxRouteLen = 8;  // ";W20,;S0"
constexpr uint8_t kMaxArgs = 7;

void emit(Out &out, ReplyPort &port) {
    if (out.finish())
        port.send(out.str(), out.len());
}

int8_t hexValue(char ch) {
    if (ch >= '0' && ch <= '9')
        return static_cast<int8_t>(ch - '0');
    if (ch >= 'A' && ch <= 'F')
        return static_cast<int8_t>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f')
        return static_cast<int8_t>(ch - 'a' + 10);
    return -1;
}

// Exactly eight hex digits, either case, then the end of the line.
bool parseCrc(const char *s, uint32_t *out) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        const int8_t hi = hexValue(s[2 * i]);
        if (hi < 0)
            return false;
        const int8_t lo = hexValue(s[2 * i + 1]);
        if (lo < 0)
            return false;
        v = (v << 8) | static_cast<uint8_t>((hi << 4) | lo);
    }
    if (s[8] != '\0')
        return false;
    *out = v;
    return true;
}

// Hex text to bytes, NUL-terminated. "-" decodes to "". Refuses odd lengths, bad digits,
// NUL bytes and anything longer than `max` bytes.
bool decodeHex(const char *hex, char *out, uint8_t max) {
    if (strcmp(hex, "-") == 0) {
        out[0] = '\0';
        return true;
    }
    const size_t n = strlen(hex);
    if (n == 0 || n % 2 != 0 || n / 2 > max)
        return false;
    for (size_t i = 0; i < n; i += 2) {
        const int8_t hi = hexValue(hex[i]);
        const int8_t lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0 || (hi == 0 && lo == 0))
            return false;
        out[i / 2] = static_cast<char>((hi << 4) | lo);
    }
    out[n / 2] = '\0';
    return true;
}

// "!" and the text's bytes in upper-case hex, NUL-terminated: how a raw command travels.
void rawToken(const char *text, char *out, uint8_t cap) {
    uint8_t n = 0;
    out[n++] = '!';
    for (; *text != '\0' && n + 2 < cap; ++text) {
        const uint8_t b = static_cast<uint8_t>(*text);
        const uint8_t hi = static_cast<uint8_t>(b >> 4);
        const uint8_t lo = static_cast<uint8_t>(b & 0x0F);
        out[n++] = static_cast<char>(hi < 10 ? '0' + hi : 'A' - 10 + hi);
        out[n++] = static_cast<char>(lo < 10 ? '0' + lo : 'A' - 10 + lo);
    }
    out[n] = '\0';
}

bool parseRoute(const char *s, uint8_t *out) {
    uint16_t v = 0;
    uint8_t n = 0;
    for (; s[n] != '\0'; ++n) {
        if (s[n] < '0' || s[n] > '9' || n >= 2)
            return false;
        v = static_cast<uint16_t>(v * 10 + (s[n] - '0'));
    }
    if (n == 0 || v > WcbPort::kMaxRoute)
        return false;
    *out = static_cast<uint8_t>(v);
    return true;
}

bool parseSlotArgs(char *const *argv, uint8_t *slot) {
    Ctrl c;
    Mod m;
    Btn b;
    Tap t;
    if (!parseCtrl(argv[0], &c) || !parseMod(argv[1], &m) || !parseBtn(argv[2], &b, &t))
        return false;
    *slot = slotIndex(c, m, b, t);
    return true;
}

bool isVerbToken(const char *s) {
    uint8_t n = 0;
    for (; s[n] != '\0'; ++n) {
        const char ch = s[n];
        if (n >= 10 || !((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'))
            return false;
    }
    return n > 0;
}

void sendError(ReplyPort &port, PGM_P code, const char *verb, uint8_t slot) {
    Out out;
    out.p(PSTR("@SM ERR ")).p(code);
    if (verb != nullptr && isVerbToken(verb))
        out.c(' ').s(verb);
    if (slot != kNoSlot)
        out.c(' ').slot(slot);
    emit(out, port);
}

char padsToken(uint8_t pads, char *out) {
    uint8_t n = 0;
    if (pads & 1)
        out[n++] = 'F';
    if (pads & 2)
        out[n++] = 'D';
    if (n == 0)
        out[n++] = '-';
    out[n] = '\0';
    return out[0];
}

PGM_P sourceToken(Source src) {
    switch (src) {
        case Source::Eeprom:
            return PSTR("eeprom");
        case Source::Reserved:
            return PSTR("reserved");
        case Source::Default:
            break;
    }
    return PSTR("default");
}

// A SLOT line's text after the slot's name: " <key|!hex|-> <source> def=<default|->", or
// " - reserved res=<reason>". `key` is what keyForSlot() gave; a Source::Default key is the
// slot's default (never raw), so it isn't looked up twice.
void slotRest(Out &out, const MapStore &store, uint8_t slot, const char *key, Source src, bool raw) {
    out.c(' ').value(key, raw).c(' ').p(sourceToken(src));
    const Reserved why = slotReserved(slot);
    if (why != Reserved::No) {
        out.p(PSTR(" res=")).p(reservedToken(why));
    } else if (src == Source::Default) {
        out.p(PSTR(" def=")).key(key);
    } else {
        char def[kKeyBuf];
        store.defaultKey(slot, def);
        out.p(PSTR(" def=")).key(def);
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------
// WcbPort

bool WcbPort::canSend(uint8_t len) {
    // Sized for the longest route so a HELLO that lengthens it can't break the headroom.
    const uint16_t framed = static_cast<uint16_t>(kMaxRouteLen + len + 1);
    return tx_.freeSpace() >= framed + kCommandHeadroom;
}

void WcbPort::send(const char *line, uint8_t len) { tx_.pushLine2(routeText_, routeLen_, line, len); }

bool WcbPort::setRoute(uint8_t wcb) {
    if (wcb > kMaxRoute)
        return false;
    route_ = wcb;
    buildRoute();
    return true;
}

void WcbPort::buildRoute() {
    uint8_t n = 0;
    if (route_ != 0) {
        routeText_[n++] = ';';
        routeText_[n++] = 'W';
        if (route_ >= 10)
            routeText_[n++] = static_cast<char>('0' + route_ / 10);
        routeText_[n++] = static_cast<char>('0' + route_ % 10);
        if (route_ >= 10)
            routeText_[n++] = ',';  // two-digit targets need the comma form: ;W12,<payload>
    }
    routeText_[n++] = ';';
    routeText_[n++] = 'S';
    routeText_[n++] = '0';
    routeText_[n] = '\0';
    routeLen_ = n;
}

// ---------------------------------------------------------------------------------------
// Protocol

Protocol::Hold Protocol::holdFor(const char *line, ReplyPort &port) {
    if (!port.canSend(kMaxLine))
        return Hold::WaitDrain;
    if (slotOut_ == kNoSlot)
        return Hold::Take;
    // A line that may be a GET ("@SM GET" and a space or its end) waits too; if it isn't one it
    // only waits a pass or two longer for its error.
    const bool waits = &port == slotOutPort_ ||
                       (strncmp(line, "@SM GET", 7) == 0 && (line[7] == ' ' || line[7] == '\0'));
    if (!waits)
        return Hold::Take;
    return slotOutPort_->canSend(kMaxLine) ? Hold::Wait : Hold::WaitDrain;
}

bool Protocol::handleLine(const char *line, ReplyPort &port) {
    if (!port.canSend(kMaxLine))
        return false;
    if (slotOut_ != kNoSlot && &port == slotOutPort_)
        return false;  // its SLOT reply goes first: replies keep their requests' order

    // One pass copies the line and finds its end.
    char work[LineReader::kMaxLen + 1];
    const char *end = static_cast<const char *>(memccpy(work, line, '\0', sizeof(work)));
    const uint8_t n = end != nullptr ? static_cast<uint8_t>(end - work - 1) : 0;
    if (end == nullptr || n < 4 || strncmp(work, "@SM ", 4) != 0) {
        sendError(port, PSTR("args"), nullptr, kNoSlot);
        return true;
    }

    // Integrity: " *<crc>" at the end, over everything before it. The last " *" counts.
    char *star = nullptr;
    for (uint8_t i = n; i >= 2; --i) {
        if (work[i - 2] == ' ' && work[i - 1] == '*') {
            star = work + i - 2;
            break;
        }
    }
    if (star != nullptr) {
        uint32_t given = 0;
        const uint8_t bodyLen = static_cast<uint8_t>(star - work);
        if (!parseCrc(star + 2, &given) ||
            given != crc32Final(crc32Block(kCrc32Init, reinterpret_cast<const uint8_t *>(work), bodyLen))) {
            sendError(port, PSTR("crc"), nullptr, kNoSlot);
            return true;
        }
        *star = '\0';
    } else if (port.requiresCrc()) {
        sendError(port, PSTR("crc"), nullptr, kNoSlot);
        return true;
    }

    // Tokens after "@SM ", separated by single spaces.
    char *argv[kMaxArgs];
    uint8_t argc = 0;
    char *p = work + 4;
    while (*p != '\0') {
        if (*p == ' ' || argc == kMaxArgs) {
            sendError(port, PSTR("args"), argc > 0 ? argv[0] : nullptr, kNoSlot);
            return true;
        }
        argv[argc++] = p;
        char *space = strchr(p, ' ');
        if (space == nullptr)
            break;
        *space = '\0';
        p = space + 1;
        if (*p == '\0') {
            sendError(port, PSTR("args"), argv[0], kNoSlot);
            return true;
        }
    }
    if (argc == 0) {
        sendError(port, PSTR("args"), nullptr, kNoSlot);
        return true;
    }
    const char *verb = argv[0];
    // Each verb is compared in full only when its first character matches.
    const char v0 = verb[0];

    if (v0 == 'H' && strcmp_P(verb, PSTR("HELLO")) == 0) {
        uint8_t route = 0;
        if (argc > 2 || (argc == 2 && !parseRoute(argv[1], &route))) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        if (argc == 2)
            port.setRoute(route);
        Out out;
        out.p(PSTR("@SM READY v")).u(kProtocolVersion).p(PSTR(" fw=")).s(fw_).p(PSTR(" slots=")).u(kSlotCount);
        out.p(PSTR(" gen=")).u(store_.generation()).p(PSTR(" fmt=")).c(store_.formatted() ? '1' : '0');
        char pads[3];
        padsToken(pads_, pads);
        out.p(PSTR(" pads=")).s(pads);
        emit(out, port);
        return true;
    }

    if (v0 == 'B' && strcmp_P(verb, PSTR("BAT")) == 0) {
        if (argc != 1) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        sendBattery(port);  // fits: canSend(kMaxLine) was checked above
        return true;
    }

    if (v0 == 'C' && strcmp_P(verb, PSTR("CFG")) == 0) {
        if (argc != 1) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        Out out;
        out.p(PSTR("@SM CFG pre=")).hex(store_.prefix()).p(PSTR(" suf=")).hex(store_.suffix());
        emit(out, port);
        return true;
    }

    if (v0 == 'G' && strcmp_P(verb, PSTR("GET")) == 0) {
        uint8_t slot = kNoSlot;
        if (argc != 4 || !parseSlotArgs(argv + 1, &slot)) {
            sendError(port, PSTR("slot"), verb, kNoSlot);
            return true;
        }
        if (slotOut_ != kNoSlot)
            return false;  // one SLOT reply in hand at a time
        return readSlot(slot, port);  // false: EEPROM busy, try again next pass
    }

    if (v0 == 'E' && strcmp_P(verb, PSTR("EVENTS")) == 0) {
        if (argc != 2 || (strcmp(argv[1], "0") != 0 && strcmp(argv[1], "1") != 0)) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        const bool on = argv[1][0] == '1';
        if (on) {
            eventPort_ = &port;
            padsReported_ = pads_;
        } else if (eventPort_ == &port) {
            eventPort_ = nullptr;
        }
        Out out;
        out.p(PSTR("@SM OK EVENTS ")).c(on ? '1' : '0');
        emit(out, port);
        return true;
    }

    const bool isSet = v0 == 'S' && strcmp_P(verb, PSTR("SET")) == 0;
    const bool isRevert = v0 == 'R' && strcmp_P(verb, PSTR("REVERT")) == 0;
    const bool isSum = v0 == 'S' && strcmp_P(verb, PSTR("SUM")) == 0;
    const bool isDump = v0 == 'D' && strcmp_P(verb, PSTR("DUMP")) == 0;
    const bool isDefaults = v0 == 'D' && strcmp_P(verb, PSTR("DEFAULTS")) == 0;
    const bool isPrefix = v0 == 'P' && strcmp_P(verb, PSTR("PREFIX")) == 0;
    const bool isSuffix = v0 == 'S' && strcmp_P(verb, PSTR("SUFFIX")) == 0;
    if (!(isSet || isRevert || isSum || isDump || isDefaults || isPrefix || isSuffix)) {
        sendError(port, PSTR("verb"), verb, kNoSlot);
        return true;
    }

    uint8_t slot = kNoSlot;
    if ((isSet && (argc != 5 || !parseSlotArgs(argv + 1, &slot))) ||
        (isRevert && (argc != 4 || !parseSlotArgs(argv + 1, &slot)))) {
        sendError(port, PSTR("slot"), verb, kNoSlot);
        return true;
    }
    if (op_ != Op::None) {
        sendError(port, PSTR("busy"), verb, slot);
        return true;
    }

    if (isDump || isSum) {
        if (argc != 1) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        op_ = isDump ? Op::Dump : Op::Sum;
        opPort_ = &port;
        dumpNext_ = 0;
        sumCrc_ = kCrc32Init;
        return true;
    }

    if (isRevert) {
        startOp(Op::Revert, port, store_.beginRevert(slot), verb, slot);
        return true;
    }

    if (isSet) {
        // A raw command travels as "!" and its bytes in hex, so its own ';', '^' or ";T" never
        // appear in a line the WCB carries. Anything that isn't 1-15 decodable bytes is "key";
        // decodeHex reads "-" as nothing, so "!-" is refused here.
        const char *arg = argv[4];
        const bool raw = arg[0] == '!';
        char text[kKeyBuf];
        const char *key = arg;
        if (raw) {
            if (!decodeHex(arg + 1, text, kKeyMax) || text[0] == '\0') {
                sendError(port, PSTR("key"), verb, slot);
                return true;
            }
            key = text;
        } else if (strcmp(arg, "-") == 0) {
            key = "";
        }
        const MapStore::Result r = store_.beginSet(slot, key, raw);
        if (r == MapStore::Result::Ok) {
            if (raw) {
                rawToken(key, opArg_, sizeof(opArg_));  // upper case, however the request spelled it
            } else {
                strncpy(opArg_, key, sizeof(opArg_) - 1);
                opArg_[sizeof(opArg_) - 1] = '\0';
            }
        }
        startOp(Op::Set, port, r, verb, slot);
        return true;
    }

    if (isDefaults) {
        if (argc != 1) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        startOp(Op::Defaults, port, store_.beginDefaults(), verb, kNoSlot);
        return true;
    }

    // PREFIX / SUFFIX: the value travels hex-encoded so no WCB control character is ever in
    // the line.
    char value[kPrefixMax + 1];
    const uint8_t max = isPrefix ? kPrefixMax : kSuffixMax;
    if (argc != 2 || !decodeHex(argv[1], value, max) || (isPrefix && value[0] == '\0')) {
        sendError(port, PSTR("args"), verb, kNoSlot);
        return true;
    }
    const MapStore::Result r = isPrefix ? store_.beginPrefix(value) : store_.beginSuffix(value);
    if (r == MapStore::Result::Ok) {
        strncpy(opArg_, argv[1], sizeof(opArg_) - 1);
        opArg_[sizeof(opArg_) - 1] = '\0';
    }
    startOp(isPrefix ? Op::Prefix : Op::Suffix, port, r, verb, kNoSlot);
    return true;
}

void Protocol::startOp(Op op, ReplyPort &port, MapStore::Result r, const char *verb, uint8_t slot) {
    switch (r) {
        case MapStore::Result::Ok:
            op_ = op;
            opPort_ = &port;
            opSlot_ = slot;
            replyPending_ = false;
            return;
        case MapStore::Result::Busy:
            sendError(port, PSTR("busy"), verb, slot);
            return;
        case MapStore::Result::BadSlot:
            sendError(port, PSTR("slot"), verb, slot);
            return;
        case MapStore::Result::Reserved:
            sendError(port, PSTR("reserved"), verb, slot);
            return;
        case MapStore::Result::BadKey:
            sendError(port, PSTR("key"), verb, slot);
            return;
        case MapStore::Result::BadArg:
            sendError(port, op == Op::Prefix ? PSTR("prefix") : PSTR("suffix"), verb, slot);
            return;
    }
}

bool Protocol::serviceLine() {
    if (servicePads())
        return true;
    if (slotOut_ != kNoSlot && sendSlotReply())
        return true;
    if (op_ == Op::None || opPort_ == nullptr)
        return false;

    if (op_ == Op::Sum) {
        // CRC32 of every SLOT line's text after "@SM SLOT " (as DUMP sends it), each followed
        // by '\n': one slot per pass (serviceSum), one reply at the end.
        if (dumpNext_ < kSlotCount || !opPort_->canSend(kMaxLine))
            return false;
        Out out;
        out.p(PSTR("@SM SUM ")).hex32(crc32Final(sumCrc_)).p(PSTR(" slots=")).u(kSlotCount);
        emit(out, *opPort_);
        op_ = Op::None;
        return true;
    }

    if (op_ == Op::Dump) {
        // A slot is read on one pass and its line sent from the reply in hand on a later one.
        if (slotOut_ != kNoSlot || !opPort_->canSend(kMaxLine))
            return false;
        if (dumpNext_ >= kSlotCount) {
            Out out;
            out.p(PSTR("@SM END DUMP ")).u(kSlotCount);
            emit(out, *opPort_);
            op_ = Op::None;
            return true;
        }
        if (!readSlot(dumpNext_, *opPort_))
            return false;  // EEPROM busy: next pass
        ++dumpNext_;
        return true;
    }

    if (!replyPending_) {
        const MapStore::Completion c = store_.takeCompletion();
        if (c == MapStore::Completion::None)
            return false;
        opOk_ = c == MapStore::Completion::Done;
        replyPending_ = true;
    }
    if (!opPort_->canSend(kMaxLine))
        return false;
    sendOpReply(opOk_);
    replyPending_ = false;
    op_ = Op::None;
    return true;
}

bool Protocol::serviceSum() {
    if (op_ != Op::Sum || opPort_ == nullptr || dumpNext_ >= kSlotCount)
        return false;
    char key[kKeyBuf];
    Source src = Source::Default;
    bool raw = false;
    if (!store_.keyForSlot(dumpNext_, key, &src, &raw))
        return false;  // EEPROM busy: next pass
    Out body;
    body.slot(dumpNext_);
    slotRest(body, store_, dumpNext_, key, src, raw);
    sumCrc_ = crc32Block(sumCrc_, reinterpret_cast<const uint8_t *>(body.str()), body.len());
    sumCrc_ = crc32Update(sumCrc_, '\n');
    ++dumpNext_;
    return true;
}

void Protocol::sendOpReply(bool ok) {
    PGM_P verb = PSTR("SET");
    switch (op_) {
        case Op::Revert:
            verb = PSTR("REVERT");
            break;
        case Op::Defaults:
            verb = PSTR("DEFAULTS");
            break;
        case Op::Prefix:
            verb = PSTR("PREFIX");
            break;
        case Op::Suffix:
            verb = PSTR("SUFFIX");
            break;
        default:
            break;
    }
    Out out;
    if (!ok) {
        out.p(PSTR("@SM ERR eeprom ")).p(verb);
        if (op_ == Op::Set || op_ == Op::Revert)
            out.c(' ').slot(opSlot_);
        emit(out, *opPort_);
        return;
    }
    out.p(PSTR("@SM OK ")).p(verb);
    if (op_ == Op::Set)
        out.c(' ').slot(opSlot_).c(' ').key(opArg_);
    else if (op_ == Op::Revert)
        out.c(' ').slot(opSlot_);
    else if (op_ == Op::Defaults)
        out.p(PSTR(" gen=")).u(store_.generation());
    else
        out.c(' ').s(opArg_);
    emit(out, *opPort_);
}

// A SLOT reply's first pass: reads the slot into the reply in hand. False, with nothing read,
// while the EEPROM is mid-write.
bool Protocol::readSlot(uint8_t slot, ReplyPort &port) {
    if (!store_.keyForSlot(slot, slotOutKey_, &slotOutSrc_, &slotOutRaw_))
        return false;
    slotOut_ = slot;
    slotOutPort_ = &port;
    return true;
}

// Its second: formats and sends the slot read on an earlier pass, once the port has room.
bool Protocol::sendSlotReply() {
    if (!slotOutPort_->canSend(kMaxLine))
        return false;
    Out out;
    out.head(Out::Head::Slot, slotOut_);
    slotRest(out, store_, slotOut_, slotOutKey_, slotOutSrc_, slotOutRaw_);
    emit(out, *slotOutPort_);
    slotOut_ = kNoSlot;
    return true;
}

void Protocol::notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther, bool raw) {
    // Sent if the event port has room for this line (not for kMaxLine: the press's command is
    // already queued, and with a 96-character kMaxLine any command over 21 bytes would drop the
    // event every time). Without room for even the shortest PRESS, nothing is built.
    if (eventPort_ == nullptr || !eventPort_->canSend(kPressLineMin))
        return;
    Out out;
    out.head(Out::Head::Press, slot).c(' ').value(key, raw).c(' ').p(sourceToken(src));
    const SlotParts parts = splitSlot(slot);  // slot < kSlotCount: buttons() only fires slots
    if (modFromOther)
        out.p(PSTR(" from=")).c(parts.ctrl == static_cast<uint8_t>(Ctrl::F) ? 'D' : 'F');
    const Reserved why = slot < kSlotCount ? slotReserved(parts) : Reserved::No;
    if (why != Reserved::No)
        out.p(PSTR(" res=")).p(reservedToken(why));
    if (!eventPort_->canSend(static_cast<uint8_t>(out.len() + 10)))  // the line and its " *<CRC>"
        return;
    emit(out, *eventPort_);
}

namespace {

PGM_P battLevelToken(BattLevel level) {
    switch (level) {
        case BattLevel::Ok:
            return PSTR("OK");
        case BattLevel::Low:
            return PSTR("LOW");
        case BattLevel::Crit:
            return PSTR("CRIT");
        default:
            return PSTR("-");
    }
}

// The raw status byte: "0".."5" on battery, "chg" charging or "usb" charged on a cable, "-" none.
void battStatusToken(Out &out, uint8_t status) {
    if (status == 0)
        out.c('-');
    else if (status >= 0xEE)
        out.p((status & 1) != 0 ? PSTR("usb") : PSTR("chg"));
    else
        out.u(status);
}

}  // namespace

bool Protocol::sendBattery(ReplyPort &port) {
    if (!port.canSend(kMaxLine))
        return false;
    Out out;
    out.p(PSTR("@SM BAT F=")).p(battLevelToken(battery_.foot)).p(PSTR(" D=")).p(battLevelToken(battery_.dome));
    out.p(PSTR(" fs="));
    battStatusToken(out, battery_.footStatus);
    out.p(PSTR(" ds="));
    battStatusToken(out, battery_.domeStatus);
    out.p(PSTR(" up=")).uThousandths(battery_.nowMs).p(PSTR(" hb=")).u(kBatteryPeriodMs / 1000);
    emit(out, port);
    return true;
}

void Protocol::setPads(bool footConnected, bool domeConnected) {
    pads_ = static_cast<uint8_t>((footConnected ? 1 : 0) | (domeConnected ? 2 : 0));
}

// With EVENTS on, one "@SM CTL <F|D> <0|1>" per controller that connected or dropped.
bool Protocol::servicePads() {
    if (eventPort_ == nullptr || pads_ == padsReported_ || !eventPort_->canSend(kMaxLine))
        return false;
    const uint8_t changed = static_cast<uint8_t>(pads_ ^ padsReported_);
    const uint8_t bit = (changed & 1) ? 1 : 2;
    Out out;
    out.p(PSTR("@SM CTL ")).c(bit == 1 ? 'F' : 'D').c(' ').c((pads_ & bit) ? '1' : '0');
    emit(out, *eventPort_);
    padsReported_ = static_cast<uint8_t>(padsReported_ ^ bit);
    return true;
}

}  // namespace sm
