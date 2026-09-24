#include "sm_protocol.h"

#include <string.h>

#include "sm_crc.h"
#include "sm_line.h"
#include "sm_pgm.h"

namespace sm {

namespace {

constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kMaxRouteLen = 8;  // ";W20,;S0"
constexpr uint8_t kMaxArgs = 7;

const char kHexDigits[] = "0123456789ABCDEF";

// Builds one reply line in a fixed buffer. Any overflow makes finish() fail, so a line is
// either sent whole or not at all.
class Out {
  public:
    Out() { buf_[0] = '\0'; }

    Out &p(PGM_P s) {
        const size_t n = strlen_P(s);
        if (reserve(n)) {
            memcpy_P(buf_ + len_, s, n);
            advance(n);
        }
        return *this;
    }
    Out &s(const char *s) {
        const size_t n = strlen(s);
        if (reserve(n)) {
            memcpy(buf_ + len_, s, n);
            advance(n);
        }
        return *this;
    }
    Out &c(char ch) {
        if (reserve(1)) {
            buf_[len_] = ch;
            advance(1);
        }
        return *this;
    }
    Out &u(uint16_t v) {
        char tmp[6];
        uint8_t n = 0;
        do {
            tmp[n++] = static_cast<char>('0' + v % 10);
            v = static_cast<uint16_t>(v / 10);
        } while (v != 0);
        while (n > 0)
            c(tmp[--n]);
        return *this;
    }
    // Each byte as two hex digits; an empty string is written as "-".
    Out &hex(const char *s) {
        if (*s == '\0')
            return c('-');
        for (; *s != '\0'; ++s) {
            const uint8_t b = static_cast<uint8_t>(*s);
            c(kHexDigits[b >> 4]).c(kHexDigits[b & 0x0F]);
        }
        return *this;
    }
    Out &key(const char *k) { return *k == '\0' ? c('-') : s(k); }
    Out &slot(uint8_t slot) {
        char tmp[16];
        if (formatSlot(tmp, sizeof(tmp), slot) == 0)
            overflow_ = true;
        return s(tmp);
    }
    bool finish() {
        const uint32_t crc = crc32(reinterpret_cast<const uint8_t *>(buf_), len_);
        c(' ').c('*');
        for (int8_t shift = 28; shift >= 0; shift -= 4)
            c(kHexDigits[(crc >> shift) & 0x0F]);
        return !overflow_;
    }
    const char *str() const { return buf_; }
    uint8_t len() const { return len_; }

  private:
    bool reserve(size_t n) {
        if (overflow_ || len_ + n > Protocol::kMaxLine) {
            overflow_ = true;
            return false;
        }
        return true;
    }
    void advance(size_t n) {
        len_ = static_cast<uint8_t>(len_ + n);
        buf_[len_] = '\0';
    }

    char buf_[Protocol::kMaxLine + 1];
    uint8_t len_ = 0;
    bool overflow_ = false;
};

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

bool parseCrc(const char *s, uint32_t *out) {
    uint32_t v = 0;
    uint8_t n = 0;
    for (; s[n] != '\0'; ++n) {
        const int8_t d = hexValue(s[n]);
        if (d < 0 || n >= 8)
            return false;
        v = (v << 4) | static_cast<uint8_t>(d);
    }
    if (n != 8)
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
    if (!parseCtrl(argv[0], &c) || !parseMod(argv[1], &m) || !parseBtn(argv[2], &b))
        return false;
    *slot = slotIndex(c, m, b);
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

}  // namespace

// ---------------------------------------------------------------------------------------
// WcbPort

bool WcbPort::canSend(uint8_t len) {
    // Sized for the longest route so a HELLO that lengthens it can't break the headroom.
    const uint16_t framed = static_cast<uint16_t>(kMaxRouteLen + len + 1);
    return tx_.freeSpace() >= framed + kCommandHeadroom;
}

void WcbPort::send(const char *line, uint8_t len) {
    (void)len;
    const char *parts[] = {routeText_, line};
    tx_.pushLine(parts, 2);
}

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
}

// ---------------------------------------------------------------------------------------
// Protocol

bool Protocol::handleLine(const char *line, ReplyPort &port) {
    if (!port.canSend(kMaxLine))
        return false;

    char work[LineReader::kMaxLen + 1];
    const size_t n = strlen(line);
    if (n > LineReader::kMaxLen || n < 4 || strncmp(line, "@SM ", 4) != 0) {
        sendError(port, PSTR("args"), nullptr, kNoSlot);
        return true;
    }
    memcpy(work, line, n + 1);

    // Integrity: " *<crc>" at the end, over everything before it.
    char *star = nullptr;
    for (char *p = work; (p = strstr(p, " *")) != nullptr; ++p)
        star = p;
    if (star != nullptr) {
        uint32_t given = 0;
        const size_t bodyLen = static_cast<size_t>(star - work);
        if (!parseCrc(star + 2, &given) || given != crc32(reinterpret_cast<const uint8_t *>(work), bodyLen)) {
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

    if (strcmp_P(verb, PSTR("HELLO")) == 0) {
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

    if (strcmp_P(verb, PSTR("CFG")) == 0) {
        if (argc != 1) {
            sendError(port, PSTR("args"), verb, kNoSlot);
            return true;
        }
        Out out;
        out.p(PSTR("@SM CFG pre=")).hex(store_.prefix()).p(PSTR(" suf=")).hex(store_.suffix());
        emit(out, port);
        return true;
    }

    if (strcmp_P(verb, PSTR("GET")) == 0) {
        uint8_t slot = kNoSlot;
        if (argc != 4 || !parseSlotArgs(argv + 1, &slot)) {
            sendError(port, PSTR("slot"), verb, kNoSlot);
            return true;
        }
        return sendSlotLine(slot, port);  // false: EEPROM busy, try again next pass
    }

    if (strcmp_P(verb, PSTR("EVENTS")) == 0) {
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

    const bool isSet = strcmp_P(verb, PSTR("SET")) == 0;
    const bool isRevert = strcmp_P(verb, PSTR("REVERT")) == 0;
    const bool isSum = strcmp_P(verb, PSTR("SUM")) == 0;
    const bool isDump = strcmp_P(verb, PSTR("DUMP")) == 0;
    const bool isDefaults = strcmp_P(verb, PSTR("DEFAULTS")) == 0;
    const bool isPrefix = strcmp_P(verb, PSTR("PREFIX")) == 0;
    const bool isSuffix = strcmp_P(verb, PSTR("SUFFIX")) == 0;
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
        const char *key = strcmp(argv[4], "-") == 0 ? "" : argv[4];
        const MapStore::Result r = store_.beginSet(slot, key);
        if (r == MapStore::Result::Ok) {
            strncpy(opArg_, key, sizeof(opArg_) - 1);
            opArg_[sizeof(opArg_) - 1] = '\0';
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

void Protocol::service() {
    servicePads();
    if (op_ == Op::None || opPort_ == nullptr)
        return;

    if (op_ == Op::Sum) {
        // CRC32 of every SLOT line's text after "@SM SLOT " (as DUMP sends it), each followed
        // by '\n': one slot per pass, one reply at the end.
        if (dumpNext_ < kSlotCount) {
            char key[kKeyBuf];
            Source src = Source::Default;
            if (!store_.keyForSlot(dumpNext_, key, &src))
                return;  // EEPROM busy: next pass
            char body[Protocol::kMaxLine + 1];
            const uint8_t n = formatSlotBody(body, sizeof(body), dumpNext_, key, src);
            for (uint8_t i = 0; i < n; ++i)
                sumCrc_ = crc32Update(sumCrc_, static_cast<uint8_t>(body[i]));
            sumCrc_ = crc32Update(sumCrc_, '\n');
            ++dumpNext_;
            return;
        }
        if (!opPort_->canSend(kMaxLine))
            return;
        Out out;
        const uint32_t crc = crc32Final(sumCrc_);
        out.p(PSTR("@SM SUM "));
        for (int8_t shift = 28; shift >= 0; shift -= 4)
            out.c(kHexDigits[(crc >> shift) & 0x0F]);
        out.p(PSTR(" slots=")).u(kSlotCount);
        emit(out, *opPort_);
        op_ = Op::None;
        return;
    }

    if (op_ == Op::Dump) {
        if (!opPort_->canSend(kMaxLine))
            return;
        if (dumpNext_ >= kSlotCount) {
            Out out;
            out.p(PSTR("@SM END DUMP ")).u(kSlotCount);
            emit(out, *opPort_);
            op_ = Op::None;
            return;
        }
        if (sendSlotLine(dumpNext_, *opPort_))
            ++dumpNext_;
        return;
    }

    if (!replyPending_) {
        const MapStore::Completion c = store_.takeCompletion();
        if (c == MapStore::Completion::None)
            return;
        opOk_ = c == MapStore::Completion::Done;
        replyPending_ = true;
    }
    if (!opPort_->canSend(kMaxLine))
        return;
    sendOpReply(opOk_);
    replyPending_ = false;
    op_ = Op::None;
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

bool Protocol::sendSlotLine(uint8_t slot, ReplyPort &port) {
    char key[kKeyBuf];
    Source src = Source::Default;
    if (!store_.keyForSlot(slot, key, &src))
        return false;
    char body[kMaxLine + 1];
    formatSlotBody(body, sizeof(body), slot, key, src);
    Out out;
    out.p(PSTR("@SM SLOT ")).s(body);
    emit(out, port);
    return true;
}

// "<slot> <key|-> <source> def=<default|->", or "<slot> - reserved res=<reason>".
uint8_t Protocol::formatSlotBody(char *dst, uint8_t cap, uint8_t slot, const char *key, Source src) const {
    Out out;
    out.slot(slot).c(' ').key(key).c(' ').p(sourceToken(src));
    const Reserved why = slotReserved(slot);
    if (why != Reserved::No) {
        out.p(PSTR(" res=")).p(reservedToken(why));
    } else {
        char def[kKeyBuf];
        store_.defaultKey(slot, def);
        out.p(PSTR(" def=")).key(def);
    }
    const uint8_t n = out.len() < cap ? out.len() : static_cast<uint8_t>(cap - 1);
    memcpy(dst, out.str(), n);
    dst[n] = '\0';
    return n;
}

void Protocol::notifyPress(uint8_t slot, const char *key, Source src, bool modFromOther) {
    if (eventPort_ == nullptr || !eventPort_->canSend(kMaxLine))
        return;
    Out out;
    out.p(PSTR("@SM PRESS ")).slot(slot).c(' ').key(key).c(' ').p(sourceToken(src));
    if (modFromOther)
        out.p(PSTR(" from=")).c(slotCtrl(slot) == Ctrl::F ? 'D' : 'F');
    const Reserved why = slotReserved(slot);
    if (why != Reserved::No)
        out.p(PSTR(" res=")).p(reservedToken(why));
    emit(out, *eventPort_);
}

void Protocol::setPads(bool footConnected, bool domeConnected) {
    pads_ = static_cast<uint8_t>((footConnected ? 1 : 0) | (domeConnected ? 2 : 0));
}

// With EVENTS on, one "@SM CTL <F|D> <0|1>" per controller that connected or dropped.
void Protocol::servicePads() {
    if (eventPort_ == nullptr || pads_ == padsReported_ || !eventPort_->canSend(kMaxLine))
        return;
    const uint8_t changed = static_cast<uint8_t>(pads_ ^ padsReported_);
    const uint8_t bit = (changed & 1) ? 1 : 2;
    Out out;
    out.p(PSTR("@SM CTL ")).c(bit == 1 ? 'F' : 'D').c(' ').c((pads_ & bit) ? '1' : '0');
    emit(out, *eventPort_);
    padsReported_ = static_cast<uint8_t>(padsReported_ ^ bit);
}

}  // namespace sm
