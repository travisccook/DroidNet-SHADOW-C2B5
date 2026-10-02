#include "sm_tx.h"

#include <string.h>

namespace sm {

void TxQueue::put(char c) {
    buf_[tail_] = c;
    if (++tail_ == kCapacity)  // no '%': division is slow on AVR
        tail_ = 0;
    ++used_;
}

void TxQueue::putBytes(const char *s, uint8_t n) {
    const uint8_t room = static_cast<uint8_t>(kCapacity - tail_);  // up to the end of buf_
    if (n < room) {
        memcpy(buf_ + tail_, s, n);
        tail_ = static_cast<uint8_t>(tail_ + n);
    } else {
        memcpy(buf_ + tail_, s, room);
        memcpy(buf_, s + room, static_cast<uint8_t>(n - room));  // the rest wraps to the start
        tail_ = static_cast<uint8_t>(n - room);
    }
    used_ = static_cast<uint8_t>(used_ + n);
}

bool TxQueue::pushParts(const char *const *parts, const uint8_t *lens, uint8_t count) {
    uint16_t total = 1;  // '\r'
    for (uint8_t i = 0; i < count; ++i)
        total = static_cast<uint16_t>(total + lens[i]);
    if (total > freeSpace())
        return false;
    for (uint8_t i = 0; i < count; ++i) {
        if (lens[i] != 0)
            putBytes(parts[i], lens[i]);
    }
    put('\r');
    return true;
}

bool TxQueue::pushLine(const char *const *parts, uint8_t count) {
    if (count > kMaxParts)
        return false;
    uint8_t lens[kMaxParts];
    for (uint8_t i = 0; i < count; ++i) {
        const size_t n = strlen(parts[i]);
        if (n >= kCapacity)
            return false;  // can't fit, whatever else is queued
        lens[i] = static_cast<uint8_t>(n);
    }
    return pushParts(parts, lens, count);
}

bool TxQueue::pushLine2(const char *a, uint8_t na, const char *b, uint8_t nb) {
    const char *parts[] = {a, b};
    const uint8_t lens[] = {na, nb};
    return pushParts(parts, lens, 2);
}

bool pushCommand(TxQueue &tx, const char *prefix, const char *key, const char *suffix) {
    const char *parts[] = {prefix, key, suffix};
    return tx.pushLine(parts, 3);
}

bool pushCommand(TxQueue &tx, const char *prefix, uint8_t prefixLen, const char *key, const char *suffix,
                 uint8_t suffixLen) {
    const size_t keyLen = strlen(key);
    if (keyLen >= TxQueue::kCapacity)
        return false;
    const char *parts[] = {prefix, key, suffix};
    const uint8_t lens[] = {prefixLen, static_cast<uint8_t>(keyLen), suffixLen};
    return tx.pushParts(parts, lens, 3);
}

bool pushRaw(TxQueue &tx, const char *text) {
    const size_t n = strlen(text);
    if (n >= TxQueue::kCapacity)
        return false;
    return tx.pushLine2(text, static_cast<uint8_t>(n), nullptr, 0);
}

}  // namespace sm
