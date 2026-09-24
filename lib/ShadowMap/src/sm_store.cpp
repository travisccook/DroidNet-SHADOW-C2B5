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

uint16_t headerCrc(const uint8_t *hdr) {
    uint16_t crc = kCrc16Init;
    for (uint8_t i = 0; i < kHdrCrc; ++i)
        crc = crc16Update(crc, hdr[i]);
    return crc;
}

uint16_t slotCrc(uint8_t slot, uint16_t gen, const uint8_t *name) {
    uint16_t crc = kCrc16Init;
    crc = crc16Update(crc, MapStore::kLayoutVersion);
    crc = crc16Update(crc, static_cast<uint8_t>(gen & 0xFF));
    crc = crc16Update(crc, static_cast<uint8_t>(gen >> 8));
    crc = crc16Update(crc, slot);
    for (uint8_t i = 0; i < kKeyBuf; ++i)
        crc = crc16Update(crc, name[i]);
    return crc;
}

bool headerValid(const uint8_t *hdr) {
    if (hdr[kHdrMagic0] != 'S' || hdr[kHdrMagic1] != 'M' || hdr[kHdrVersion] != MapStore::kLayoutVersion ||
        hdr[kHdrSlotCount] != kSlotCount)
        return false;
    if (get16(hdr + kHdrCrc) != headerCrc(hdr))
        return false;
    if (!fieldTerminated(hdr + kHdrPrefix, kPrefixMax + 1) || !fieldTerminated(hdr + kHdrSuffix, kSuffixMax + 1))
        return false;
    return isValidPrefix(reinterpret_cast<const char *>(hdr + kHdrPrefix)) &&
           isValidSuffix(reinterpret_cast<const char *>(hdr + kHdrSuffix));
}

bool slotRecordValid(const uint8_t *rec, uint8_t slot, uint16_t gen) {
    if (!fieldTerminated(rec, kKeyBuf))
        return false;
    if (!isValidKey(reinterpret_cast<const char *>(rec)))
        return false;
    return get16(rec + kKeyBuf) == slotCrc(slot, gen, rec);
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

bool isValidPrefix(const char *prefix) {
    const size_t n = strlen(prefix);
    if (n == 0 || n > kPrefixMax || prefix[0] != ';' || prefix[n - 1] == ';')
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (!isCommandChar(prefix[i]))
            return false;
    }
    return true;
}

bool isValidSuffix(const char *suffix) {
    const size_t n = strlen(suffix);
    if (n > kSuffixMax)
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
        return;
    }

    for (uint8_t b = 0; b < kHeaderSize; ++b)
        hdr[b] = io_.read(static_cast<uint16_t>(headerAddr(active_) + b));
    applyHeader(hdr, active_);
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
    out[0] = '\0';
    for (uint8_t i = 0; i < defaultCount_; ++i) {
        if (pgm_read_byte(&defaults_[i].slot) == slot) {
            memcpy_P(out, defaults_[i].key, kKeyBuf);
            out[kKeyMax] = '\0';
            return;
        }
    }
}

bool MapStore::readSlotRecord(uint8_t slot, uint8_t rec[kSlotSize]) {
    if (!io_.ready()) {
        readWanted_ = true;
        return false;
    }
    const uint16_t addr = slotAddr(slot);
    for (uint8_t i = 0; i < kSlotSize; ++i)
        rec[i] = io_.read(static_cast<uint16_t>(addr + i));
    return true;
}

bool MapStore::keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src) {
    Source source = Source::Default;
    if (slot >= kSlotCount || slotReserved(slot) != Reserved::No) {
        out[0] = '\0';
        source = Source::Reserved;
    } else if (phase_ != Phase::Idle && kind_ == JobKind::Slot && jobIndex_ == slot) {
        // Mid-write: answer with the value being written, never a half-written one.
        memcpy(out, jobData_, kKeyBuf);
        source = Source::Eeprom;
    } else if (pendingSlot_ == slot) {
        memcpy(out, pendingKey_, kKeyBuf);
        source = Source::Eeprom;
    } else if (!formatted()) {
        defaultKey(slot, out);
    } else {
        uint8_t rec[kSlotSize];
        if (!readSlotRecord(slot, rec))
            return false;
        if (slotRecordValid(rec, slot, gen_)) {
            memcpy(out, rec, kKeyBuf);
            source = Source::Eeprom;
        } else {
            defaultKey(slot, out);
        }
    }
    if (src != nullptr)
        *src = source;
    return true;
}

MapStore::Result MapStore::beginSet(uint8_t slot, const char *key) {
    if (busy())
        return Result::Busy;
    if (slot >= kSlotCount)
        return Result::BadSlot;
    if (slotReserved(slot) != Reserved::No)
        return Result::Reserved;
    if (!isValidKey(key))
        return Result::BadKey;
    completion_ = Completion::None;
    if (!formatted()) {
        pendingSlot_ = slot;
        strcpy(pendingKey_, key);
        startHeaderJob(0, 1, gen_, prefix_, suffix_);
    } else {
        startSlotJob(slot, key);
    }
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
    jobData_[kHdrSlotCount] = kSlotCount;
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

void MapStore::startSlotJob(uint8_t slot, const char *key) {
    memset(jobData_, 0, sizeof(jobData_));
    copyField(jobData_, key, kKeyBuf);
    put16(jobData_ + kKeyBuf, slotCrc(slot, gen_, jobData_));

    kind_ = JobKind::Slot;
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
        while (jobPos_ < jobLen_) {
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

    bool ok = true;
    for (uint8_t i = 0; i < jobLen_ && ok; ++i)
        ok = io_.read(static_cast<uint16_t>(jobAddr_ + i)) == jobData_[i];
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
        applyHeader(jobData_, jobIndex_);
        if (pendingSlot_ != kNoSlot) {
            const uint8_t slot = pendingSlot_;
            pendingSlot_ = kNoSlot;
            startSlotJob(slot, pendingKey_);
            return;
        }
        completion_ = Completion::Done;
        return;
    }
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
