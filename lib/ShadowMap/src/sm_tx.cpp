#include "sm_tx.h"

#include <string.h>

namespace sm {

void TxQueue::put(char c) {
    buf_[tail_] = c;
    if (++tail_ == kCapacity)  // no '%': division is slow on AVR
        tail_ = 0;
    ++used_;
}

bool TxQueue::pushLine(const char *const *parts, uint8_t count) {
    size_t total = 1;  // '\r'
    for (uint8_t i = 0; i < count; ++i)
        total += strlen(parts[i]);
    if (total > freeSpace())
        return false;
    for (uint8_t i = 0; i < count; ++i) {
        for (const char *p = parts[i]; *p != '\0'; ++p)
            put(*p);
    }
    put('\r');
    return true;
}

bool pushCommand(TxQueue &tx, const char *prefix, const char *key, const char *suffix) {
    const char *parts[] = {prefix, key, suffix};
    return tx.pushLine(parts, 3);
}

}  // namespace sm
