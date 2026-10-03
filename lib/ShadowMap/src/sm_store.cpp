#include "sm_store.h"

#include <string.h>

#include "sm_crc.h"
#include "sm_pgm.h"

namespace sm {

namespace {

const char kDefaultPrefix[] = ";W1;C";

// Header field offsets.
constexpr uint8_t kHdrMagic0 = 0;
constexpr uint8_t kHdrMagic1 = 1;
constexpr uint8_t kHdrVersion = 2;
constexpr uint8_t kHdrSlotCount = 3;
constexpr uint8_t kHdrSeq = 4;
constexpr uint8_t kHdrGen = 6;
constexpr uint8_t kHdrPrefix = 8;
constexpr uint8_t kHdrSuffix = kHdrPrefix + kPrefixMax + 1;  // 20
constexpr uint8_t kHdrCrc = 30;

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }

void put16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool isCommandChar(char c) { return c >= 0x21 && c <= 0x7E && c != '^'; }

// ";T" / ";t" anywhere makes the WCB treat the whole line as a timer command.
bool hasTimerToken(const char *s) {
    for (; s[0] != '\0' && s[1] != '\0'; ++s) {
        if (s[0] == ';' && (s[1] == 'T' || s[1] == 't'))
            return true;
    }
    return false;
}

// A NUL-terminated field of `width` bytes: text of at most width-1 characters, then NULs.
bool fieldTerminated(const uint8_t *field, uint8_t width) {
    bool ended = false;
    for (uint8_t i = 0; i < width; ++i) {
        if (field[i] == 0)
            ended = true;
        else if (ended)
            return false;
    }
    return ended;
}

void copyField(uint8_t *dst, const char *src, uint8_t width) {
    memset(dst, 0, width);
    const size_t n = strlen(src);
    memcpy(dst, src, n < width ? n : width - 1);
}

uint16_t headerCrc(const uint8_t *hdr) { return crc16Block(kCrc16Init, hdr, kHdrCrc); }

uint16_t slotCrc(uint8_t slot, uint16_t gen, const uint8_t *name) {
    const uint8_t head[4] = {MapStore::kLayoutVersion, static_cast<uint8_t>(gen & 0xFF), static_cast<uint8_t>(gen >> 8),
                             slot};
    return crc16Block(crc16Block(kCrc16Init, head, sizeof(head)), name, kKeyBuf);
}

bool headerValid(const uint8_t *hdr) {
    if (hdr[kHdrMagic0] != 'S' || hdr[kHdrMagic1] != 'M' || hdr[kHdrVersion] != MapStore::kLayoutVersion ||
        hdr[kHdrSlotCount] != kSingleSlotCount)
        return false;
    if (get16(hdr + kHdrCrc) != headerCrc(hdr))
        return false;
    if (!fieldTerminated(hdr + kHdrPrefix, kPrefixMax + 1) || !fieldTerminated(hdr + kHdrSuffix, kSuffixMax + 1))
        return false;
    return isValidPrefix(reinterpret_cast<const char *>(hdr + kHdrPrefix)) &&
           isValidSuffix(reinterpret_cast<const char *>(hdr + kHdrSuffix));
}

inline bool isRawChar(uint8_t c) { return c >= 0x20 && c <= 0x7E; }

// A record holds a key (0-15 key characters, then only NULs, at least one) or a raw command (its
// first byte with kRawFlag set, 1-15 printable characters in all, then only NULs), and its CRC.
// The checks run cheapest first: the first byte (a record the name check would refuse there,
// such as an erased one), the CRC, then the whole name. The result is the same as checking all
// of fieldTerminated() && (isValidKey() || the raw rule) && the CRC.
bool slotRecordValid(const uint8_t *rec, uint8_t slot, uint16_t gen) {
    const uint8_t first = rec[0];
    const bool raw = (first & kRawFlag) != 0;
    if (raw ? !isRawChar(static_cast<uint8_t>(first & 0x7F)) : (first != 0 && !isKeyChar(static_cast<char>(first))))
        return false;
    if (get16(rec + kKeyBuf) != slotCrc(slot, gen, rec))
        return false;
    uint8_t i = raw ? 1 : 0;
    for (; i < kKeyBuf && rec[i] != 0; ++i) {
        if (raw ? !isRawChar(rec[i]) : !isKeyChar(static_cast<char>(rec[i])))
            return false;
    }
    if (i == kKeyBuf)
        return false;  // 16 characters: no NUL
    for (; i < kKeyBuf; ++i) {
        if (rec[i] != 0)
            return false;
    }
    return true;
}

// A record's (or a job's) name field as text, with the raw flag cleared and reported.
void readName(const uint8_t *name, char out[kKeyBuf], bool *raw) {
    memcpy(out, name, kKeyBuf);
    out[kKeyMax] = '\0';
    *raw = (name[0] & kRawFlag) != 0;
    out[0] = static_cast<char>(name[0] & 0x7F);
}

}  // namespace

bool isValidKey(const char *key) {
    size_t n = 0;
    for (; key[n] != '\0'; ++n) {
        if (n >= kKeyMax || !isKeyChar(key[n]))
            return false;
    }
    return true;
}

bool isValidRaw(const char *text) {
    size_t n = 0;
    for (; text[n] != '\0'; ++n) {
        if (n >= kKeyMax || !isRawChar(static_cast<uint8_t>(text[n])))
            return false;
    }
    return n > 0;
}

bool isValidPrefix(const char *prefix) {
    const size_t n = strlen(prefix);
    if (n == 0 || n > kPrefixMax || prefix[0] != ';' || prefix[n - 1] == ';' || hasTimerToken(prefix))
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (!isCommandChar(prefix[i]))
            return false;
    }
    return true;
}

bool isValidSuffix(const char *suffix) {
    const size_t n = strlen(suffix);
    if (n > kSuffixMax || hasTimerToken(suffix))
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (!isCommandChar(suffix[i]))
            return false;
    }
    return true;
}

MapStore::MapStore(EepromIo &io, const DefaultMapping *defaults, uint8_t defaultCount, uint16_t base)
    : io_(io), defaults_(defaults), defaultCount_(defaultCount), base_(base) {
    strcpy(prefix_, kDefaultPrefix);
    suffix_[0] = '\0';
    pendingKey_[0] = '\0';
    memset(jobData_, 0, sizeof(jobData_));
}

void MapStore::begin() {
    uint8_t hdr[kHeaderSize];
    bool valid[2] = {false, false};
    uint16_t seqs[2] = {0, 0};
    uint16_t rawGenMax = 0;

    for (uint8_t i = 0; i < 2; ++i) {
        for (uint8_t b = 0; b < kHeaderSize; ++b) {
            while (!io_.ready()) {
            }
            hdr[b] = io_.read(static_cast<uint16_t>(headerAddr(i) + b));
        }
        const uint16_t rawGen = get16(hdr + kHdrGen);
        if (i == 0 || static_cast<int16_t>(rawGen - rawGenMax) > 0)
            rawGenMax = rawGen;
        valid[i] = headerValid(hdr);
        seqs[i] = get16(hdr + kHdrSeq);
    }

    active_ = kNoHeader;
    if (valid[0] && valid[1])
        active_ = static_cast<int16_t>(seqs[1] - seqs[0]) > 0 ? 1 : 0;
    else if (valid[0])
        active_ = 0;
    else if (valid[1])
        active_ = 1;

    if (active_ == kNoHeader) {
        // Unformatted: everything reads as defaults. A first write formats header A with a
        // generation past anything left in either copy, so stale slots can't come back.
        strcpy(prefix_, kDefaultPrefix);
        suffix_[0] = '\0';
        seq_ = 0;
        gen_ = static_cast<uint16_t>(rawGenMax + 1);
        loadTapDefaults();
        return;
    }

    for (uint8_t b = 0; b < kHeaderSize; ++b)
        hdr[b] = io_.read(static_cast<uint16_t>(headerAddr(active_) + b));
    applyHeader(hdr, active_);
    for (uint8_t slot = kSingleSlotCount; slot < kSlotCount; ++slot) {
        while (!io_.ready()) {
        }
        loadTap(slot);
    }
}

bool MapStore::doubleTapMapped(uint8_t slot) const {
    if (slot >= kSlotCount)
        return false;
    const uint8_t i = splitSlot(slot).bank;
    return (doubleTaps_[i >> 3] & (1u << (i & 7))) != 0;
}

bool MapStore::longPressMapped(uint8_t slot) const {
    if (slot >= kSlotCount)
        return false;
    const uint8_t i = splitSlot(slot).bank;
    return (longPresses_[i >> 3] & (1u << (i & 7))) != 0;
}

void MapStore::setTapMapped(uint8_t slot, bool mapped) {
    const SlotParts p = splitSlot(slot);
    uint8_t *bits = p.tap == static_cast<uint8_t>(Tap::LONG) ? longPresses_ : doubleTaps_;
    const uint8_t i = p.bank;
    const uint8_t bit = static_cast<uint8_t>(1u << (i & 7));
    if (mapped && slotReserved(slot) == Reserved::No)
        bits[i >> 3] = static_cast<uint8_t>(bits[i >> 3] | bit);
    else
        bits[i >> 3] = static_cast<uint8_t>(bits[i >> 3] & ~bit);
}

bool MapStore::defaultMapped(uint8_t slot) const {
    char key[kKeyBuf];
    defaultKey(slot, key);
    return key[0] != '\0';
}

// Every double-tap and long-press bit from the defaults table: one pass over it, last entry
// first, so where a slot is listed twice the first entry wins, as in defaultKey().
void MapStore::loadTapDefaults() {
    memset(doubleTaps_, 0, sizeof(doubleTaps_));
    memset(longPresses_, 0, sizeof(longPresses_));
    const DefaultMapping *d = defaults_ + defaultCount_;
    for (uint8_t n = defaultCount_; n != 0; --n) {
        --d;
        const uint8_t slot = pgm_read_byte(&d->slot);
        if (slot >= kSingleSlotCount && slot < kSlotCount)
            setTapMapped(slot, pgm_read_byte(&d->key[0]) != 0);
    }
}

void MapStore::loadTap(uint8_t slot) {
    uint8_t rec[kSlotSize];
    io_.readBlock(slotAddr(slot), rec, kSlotSize);
    setTapMapped(slot, slotRecordValid(rec, slot, gen_) ? rec[0] != 0 : defaultMapped(slot));
}

void MapStore::applyHeader(const uint8_t *hdr, uint8_t index) {
    active_ = index;
    seq_ = get16(hdr + kHdrSeq);
    gen_ = get16(hdr + kHdrGen);
    memcpy(prefix_, hdr + kHdrPrefix, sizeof(prefix_));
    prefix_[kPrefixMax] = '\0';
    memcpy(suffix_, hdr + kHdrSuffix, sizeof(suffix_));
    suffix_[kSuffixMax] = '\0';
}

void MapStore::defaultKey(uint8_t slot, char out[kKeyBuf]) const {
    const DefaultMapping *d = defaults_;
    for (uint8_t n = defaultCount_; n != 0; --n, ++d) {
        if (pgm_read_byte(&d->slot) == slot) {
            memcpy_P(out, d->key, kKeyBuf);
            out[kKeyMax] = '\0';
            return;
        }
    }
    out[0] = '\0';
}

bool MapStore::readSlotRecord(uint8_t slot, uint8_t rec[kSlotSize]) {
    if (!io_.ready()) {
        readWanted_ = true;
        return false;
    }
    io_.readBlock(slotAddr(slot), rec, kSlotSize);
    return true;
}

bool MapStore::keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src, bool *raw) {
    Source source = Source::Default;
    bool isRaw = false;
    if (slot >= kSlotCount || slotReserved(slot) != Reserved::No) {
        out[0] = '\0';
        source = Source::Reserved;
    } else if (phase_ != Phase::Idle && kind_ == JobKind::Slot && jobIndex_ == slot) {
        // Mid-write: answer with the value being written, never a half-written one.
        readName(jobData_, out, &isRaw);
        source = Source::Eeprom;
    } else if (phase_ != Phase::Idle && kind_ == JobKind::SlotErase && jobIndex_ == slot) {
        defaultKey(slot, out);
    } else if (pendingSlot_ == slot) {
        memcpy(out, pendingKey_, kKeyBuf);
        isRaw = pendingRaw_;
        source = Source::Eeprom;
    } else if (!formatted()) {
        defaultKey(slot, out);
    } else {
        uint8_t rec[kSlotSize];
        if (!readSlotRecord(slot, rec))
            return false;
        if (slotRecordValid(rec, slot, gen_)) {
            readName(rec, out, &isRaw);
            source = Source::Eeprom;
        } else {
            defaultKey(slot, out);
        }
    }
    if (src != nullptr)
        *src = source;
    if (raw != nullptr)
        *raw = isRaw;
    return true;
}

MapStore::Result MapStore::beginSet(uint8_t slot, const char *key, bool raw) {
    if (busy())
        return Result::Busy;
    if (slot >= kSlotCount)
        return Result::BadSlot;
    if (slotReserved(slot) != Reserved::No)
        return Result::Reserved;
    if (raw ? !isValidRaw(key) : !isValidKey(key))
        return Result::BadKey;
    completion_ = Completion::None;
    if (!formatted()) {
        pendingSlot_ = slot;
        strcpy(pendingKey_, key);
        pendingRaw_ = raw;
        startHeaderJob(0, 1, gen_, prefix_, suffix_);
    } else {
        startSlotJob(slot, key, raw);
    }
    return Result::Ok;
}

MapStore::Result MapStore::beginRevert(uint8_t slot) {
    if (busy())
        return Result::Busy;
    if (slot >= kSlotCount)
        return Result::BadSlot;
    if (slotReserved(slot) != Reserved::No)
        return Result::Reserved;
    if (!formatted()) {
        completion_ = Completion::Done;  // nothing in EEPROM: already the default
        return Result::Ok;
    }
    completion_ = Completion::None;
    startEraseJob(slot);
    return Result::Ok;
}

MapStore::Result MapStore::beginHeaderChange(uint16_t gen, const char *prefix, const char *suffix) {
    completion_ = Completion::None;
    if (formatted())
        startHeaderJob(static_cast<uint8_t>(active_ ^ 1), static_cast<uint16_t>(seq_ + 1), gen, prefix, suffix);
    else
        startHeaderJob(0, 1, gen, prefix, suffix);
    return Result::Ok;
}

MapStore::Result MapStore::beginDefaults() {
    if (busy())
        return Result::Busy;
    if (!formatted()) {
        completion_ = Completion::Done;  // already reading defaults; nothing to write
        return Result::Ok;
    }
    return beginHeaderChange(static_cast<uint16_t>(gen_ + 1), prefix_, suffix_);
}

MapStore::Result MapStore::beginPrefix(const char *prefix) {
    if (busy())
        return Result::Busy;
    if (!isValidPrefix(prefix))
        return Result::BadArg;
    return beginHeaderChange(gen_, prefix, suffix_);
}

MapStore::Result MapStore::beginSuffix(const char *suffix) {
    if (busy())
        return Result::Busy;
    if (!isValidSuffix(suffix))
        return Result::BadArg;
    return beginHeaderChange(gen_, prefix_, suffix);
}

void MapStore::startHeaderJob(uint8_t index, uint16_t seq, uint16_t gen, const char *prefix, const char *suffix) {
    memset(jobData_, 0, sizeof(jobData_));
    jobData_[kHdrMagic0] = 'S';
    jobData_[kHdrMagic1] = 'M';
    jobData_[kHdrVersion] = kLayoutVersion;
    jobData_[kHdrSlotCount] = kSingleSlotCount;  // see sm_store.h
    put16(jobData_ + kHdrSeq, seq);
    put16(jobData_ + kHdrGen, gen);
    copyField(jobData_ + kHdrPrefix, prefix, kPrefixMax + 1);
    copyField(jobData_ + kHdrSuffix, suffix, kSuffixMax + 1);
    put16(jobData_ + kHdrCrc, headerCrc(jobData_));

    kind_ = JobKind::Header;
    jobIndex_ = index;
    jobAddr_ = headerAddr(index);
    jobLen_ = kHeaderSize;
    jobPos_ = 0;
    retries_ = 0;
    phase_ = Phase::Write;
}

void MapStore::startSlotJob(uint8_t slot, const char *key, bool raw) {
    memset(jobData_, 0, sizeof(jobData_));
    copyField(jobData_, key, kKeyBuf);
    if (raw)
        jobData_[0] = static_cast<uint8_t>(jobData_[0] | kRawFlag);  // the CRC below covers the flag
    put16(jobData_ + kKeyBuf, slotCrc(slot, gen_, jobData_));

    kind_ = JobKind::Slot;
    jobIndex_ = slot;
    jobAddr_ = slotAddr(slot);
    jobLen_ = kSlotSize;
    jobPos_ = 0;
    retries_ = 0;
    phase_ = Phase::Write;
}

void MapStore::startEraseJob(uint8_t slot) {
    memset(jobData_, 0xFF, kSlotSize);  // erased: fails the slot CRC, so it reads as the default
    kind_ = JobKind::SlotErase;
    jobIndex_ = slot;
    jobAddr_ = slotAddr(slot);
    jobLen_ = kSlotSize;
    jobPos_ = 0;
    retries_ = 0;
    phase_ = Phase::Write;
}

void MapStore::step() {
    if (phase_ == Phase::Idle || !io_.ready())
        return;
    if (readWanted_) {
        readWanted_ = false;  // skip one pass so a waiting lookup gets the idle EEPROM
        return;
    }

    if (phase_ == Phase::Write) {
        for (uint8_t reads = 0; jobPos_ < jobLen_; ++reads) {
            if (reads == kMaxReadsPerStep)
                return;  // the next step carries on
            const uint16_t addr = static_cast<uint16_t>(jobAddr_ + jobPos_);
            const uint8_t want = jobData_[jobPos_++];
            if (io_.read(addr) != want) {
                io_.write(addr, want);
                ++writes_;
                return;
            }
        }
        phase_ = Phase::Verify;
        return;
    }

    uint8_t got[kHeaderSize];
    io_.readBlock(jobAddr_, got, jobLen_);
    const bool ok = memcmp(got, jobData_, jobLen_) == 0;
    if (!ok && retries_ < kMaxRetries) {
        ++retries_;
        jobPos_ = 0;
        phase_ = Phase::Write;
        return;
    }
    finishJob(ok);
}

void MapStore::finishJob(bool ok) {
    phase_ = Phase::Idle;
    if (kind_ == JobKind::Header) {
        if (!ok) {
            pendingSlot_ = kNoSlot;
            completion_ = Completion::Failed;
            return;
        }
        const uint16_t oldGen = gen_;
        applyHeader(jobData_, jobIndex_);
        if (gen_ != oldGen)
            loadTapDefaults();  // a new generation: every slot reads as its default
        if (pendingSlot_ != kNoSlot) {
            const uint8_t slot = pendingSlot_;
            pendingSlot_ = kNoSlot;
            startSlotJob(slot, pendingKey_, pendingRaw_);
            return;
        }
        completion_ = Completion::Done;
        return;
    }
    if (slotTap(jobIndex_) != Tap::SINGLE)
        loadTap(jobIndex_);  // what's really there now, written or not (the EEPROM is idle: step() checked)
    completion_ = ok ? Completion::Done : Completion::Failed;
}

MapStore::Completion MapStore::takeCompletion() {
    const Completion c = completion_;
    completion_ = Completion::None;
    return c;
}

uint8_t MapStore::validEepromSlots() {
    if (!formatted())
        return 0;
    uint8_t count = 0;
    uint8_t rec[kSlotSize];
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        while (!io_.ready()) {
        }
        readSlotRecord(slot, rec);
        if (slotRecordValid(rec, slot, gen_))
            ++count;
    }
    readWanted_ = false;
    return count;
}

}  // namespace sm
