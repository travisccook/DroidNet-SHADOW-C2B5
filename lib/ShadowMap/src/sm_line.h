// Non-blocking line assembly for incoming serial bytes. Only lines starting with "@SM " are
// kept; everything else (WCB broadcast traffic, debug text) is skipped a byte at a time
// without buffering it. Lines end at '\r' or '\n'.
#pragma once

#include <stdint.h>

namespace sm {

class LineReader {
  public:
    static constexpr uint8_t kMaxLen = 72;

    // Feed one byte. Returns true once a complete "@SM " line is ready; after that, bytes are
    // refused (false) until consume() is called, so a line waiting to be handled is never
    // overwritten.
    bool feed(char c);
    bool ready() const { return state_ == State::Ready; }
    const char *line() const { return buf_; }
    uint8_t length() const { return len_; }
    void consume();

  private:
    enum class State : uint8_t { Collect, Skip, Ready };
    char buf_[kMaxLen + 1] = {};
    uint8_t len_ = 0;
    State state_ = State::Collect;
};

}  // namespace sm
