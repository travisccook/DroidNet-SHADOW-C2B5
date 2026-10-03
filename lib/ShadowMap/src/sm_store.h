// EEPROM-backed mapping store.
//
// Layout, from `base` (3952 bytes in all):
//   header A (32 B), header B (32 B)   magic "SM", layout version, slot count, sequence,
//                                      generation, command prefix/suffix, CRC16
//   216 slots x 18 B                   stored-command key (NUL-padded) + CRC16 over
//                                      (layout version, generation, slot index, key)
//
// The 72 double-tap slots (72-143) follow the 72 single-press slots, which end at 1360, and
// the 72 long-press slots (144-215) follow the double taps, from 2656. The header's slot count
// stays 72, the single-press slots, so older firmware reads the same EEPROM and keeps its
// mappings; it never touches the banks it doesn't know.
//
// Headers are written alternately and the valid one with the newer sequence wins, so a
// power cut while a header is being written leaves the previous one in force. Each slot
// carries its own CRC, so a slot that fails it (a power cut mid-write, or a slot written
// under an older generation) falls back to its built-in default without affecting the
// others. Bumping the generation (DEFAULTS) invalidates every slot with one header write;
// REVERT erases a single slot the same way.
//
// All writes happen in step(), one byte per call and only when the EEPROM is idle, so the
// main loop never waits on EEPROM. Bytes that already hold the right value are skipped and
// every record is read back and verified once written. At most kMaxReadsPerStep bytes are read
// per call.
#pragma once

#include <stdint.h>

#include "sm_eeprom.h"
#include "sm_slots.h"

namespace sm {

constexpr uint8_t kKeyMax = 15;  // WCB stored-command keys are limited to 15 characters
constexpr uint8_t kKeyBuf = kKeyMax + 1;
constexpr uint8_t kPrefixMax = 11;
constexpr uint8_t kSuffixMax = 7;

// One built-in mapping. Arrays of these live in flash (PROGMEM).
struct DefaultMapping {
    uint8_t slot;
    char key[kKeyBuf];
};

enum class Source : uint8_t { Eeprom, Default, Reserved };

// "" (unmapped) or 1-15 of [A-Za-z0-9_].
bool isValidKey(const char *key);
// 1-11 printable characters, starting with ';' so a command is never a bare (broadcast)
// line, with no '^' (the WCB's delimiter) and no ";T"/";t" (a WCB timer command), and not
// ending with ';' (";" + a key starting with t would make one).
bool isValidPrefix(const char *prefix);
// 0-7 printable characters, no '^' and no ";T"/";t".
bool isValidSuffix(const char *suffix);

class MapStore {
  public:
    static constexpr uint8_t kLayoutVersion = 1;
    static constexpr uint8_t kHeaderSize = 32;
    static constexpr uint8_t kSlotSize = kKeyBuf + 2;
    static constexpr uint16_t kFootprint = 2 * kHeaderSize + static_cast<uint16_t>(kSlotCount) * kSlotSize;
    // Bytes the write phase reads in one step() before it returns: a step over bytes that already
    // hold the right value stays short (about 5 us a byte on the Mega), and the next step carries on.
    static constexpr uint8_t kMaxReadsPerStep = 4;

    enum class Result : uint8_t { Ok, Busy, BadSlot, Reserved, BadKey, BadArg };
    enum class Completion : uint8_t { None, Done, Failed };

    // `defaults` points at a flash array of `defaultCount` built-in mappings.
    MapStore(EepromIo &io, const DefaultMapping *defaults, uint8_t defaultCount, uint16_t base = 0);

    // Reads the headers. Call once from setup(), before any other method.
    void begin();

    bool formatted() const { return active_ != kNoHeader; }
    uint16_t generation() const { return gen_; }
    const char *prefix() const { return prefix_; }
    const char *suffix() const { return suffix_; }

    // The key a slot currently maps to ("" = nothing). Returns false, without touching `out`,
    // only while an EEPROM write is in flight and the slot has to be read from EEPROM; call
    // again on a later loop pass. The writer pauses for one pass to let the read through.
    bool keyForSlot(uint8_t slot, char out[kKeyBuf], Source *src = nullptr);
    void defaultKey(uint8_t slot, char out[kKeyBuf]) const;

    // Whether the double tap of `slot` (either its single-press or its double-tap index) maps
    // to a key. Kept in RAM, so it never waits on the EEPROM; a write changes it once it's
    // done.
    bool doubleTapMapped(uint8_t slot) const;

    // The same for the long press of `slot` (its single-press or its long-press index).
    bool longPressMapped(uint8_t slot) const;

    // Start a write. Only one runs at a time; watch takeCompletion() for the outcome.
    Result beginSet(uint8_t slot, const char *key);  // "" clears the slot
    Result beginRevert(uint8_t slot);                // back to the built-in default
    Result beginDefaults();
    Result beginPrefix(const char *prefix);
    Result beginSuffix(const char *suffix);

    bool busy() const { return phase_ != Phase::Idle || pendingSlot_ != kNoSlot; }
    // The next step() verifies the job's record and, if it's good, finishes the job (the heavy
    // step: a slot's RAM bit reload, or a header's apply and, for DEFAULTS, every bit's reload).
    bool finishDue() const { return phase_ == Phase::Verify; }
    void step();
    Completion takeCompletion();

    // Diagnostics. validEepromSlots() waits for the EEPROM, so only call it from setup().
    uint16_t writesIssued() const { return writes_; }
    uint8_t activeHeader() const { return active_; }
    uint8_t validEepromSlots();

  private:
    static constexpr uint8_t kNoHeader = 0xFF;
    static constexpr uint8_t kMaxRetries = 1;

    enum class Phase : uint8_t { Idle, Write, Verify };
    enum class JobKind : uint8_t { Header, Slot, SlotErase };

    uint16_t headerAddr(uint8_t index) const { return static_cast<uint16_t>(base_ + index * kHeaderSize); }
    uint16_t slotAddr(uint8_t slot) const {
        return static_cast<uint16_t>(base_ + 2 * kHeaderSize + static_cast<uint16_t>(slot) * kSlotSize);
    }

    void startHeaderJob(uint8_t index, uint16_t seq, uint16_t gen, const char *prefix, const char *suffix);
    void startSlotJob(uint8_t slot, const char *key);
    void startEraseJob(uint8_t slot);
    void finishJob(bool ok);
    void applyHeader(const uint8_t *hdr, uint8_t index);
    bool readSlotRecord(uint8_t slot, uint8_t rec[kSlotSize]);
    Result beginHeaderChange(uint16_t gen, const char *prefix, const char *suffix);
    bool defaultMapped(uint8_t slot) const;
    // The RAM copies of which double taps and long presses map to a key (by slotTap(slot)).
    void setTapMapped(uint8_t slot, bool mapped);
    void loadTapDefaults();
    void loadTap(uint8_t slot);  // from its EEPROM record; the EEPROM must be ready

    EepromIo &io_;
    const DefaultMapping *defaults_;
    uint8_t defaultCount_;
    uint16_t base_;

    uint8_t active_ = kNoHeader;
    uint16_t seq_ = 0;
    uint16_t gen_ = 0;
    char prefix_[kPrefixMax + 1];
    char suffix_[kSuffixMax + 1];

    Phase phase_ = Phase::Idle;
    JobKind kind_ = JobKind::Slot;
    uint8_t jobIndex_ = 0;  // header copy or slot index
    uint16_t jobAddr_ = 0;
    uint8_t jobLen_ = 0;
    uint8_t jobPos_ = 0;
    uint8_t retries_ = 0;
    uint8_t jobData_[kHeaderSize];

    uint8_t pendingSlot_ = kNoSlot;  // slot write queued behind a first-time format
    char pendingKey_[kKeyBuf];

    bool readWanted_ = false;
    Completion completion_ = Completion::None;
    uint8_t doubleTaps_[(kSingleSlotCount + 7) / 8] = {};  // bit per double-tap slot that maps to a key
    uint8_t longPresses_[(kSingleSlotCount + 7) / 8] = {};  // bit per long-press slot that maps to a key
    uint16_t writes_ = 0;
};

}  // namespace sm
