// Non-blocking line assembly for incoming serial bytes. Only lines starting with "@SM " are
// kept; everything else (WCB broadcast traffic, debug text) is skipped a byte at a time
// without buffering it. Lines end at '\r' or '\n'. Noise bytes before a line starts are ignored.
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

    // While a ready line waits to be handled, bytes that keep arriving are thrown away with
    // this, so the UART's small RX buffer can't overflow (and merge lines). If that stops
    // mid-line, the reader skips to the next line end after consume().
    void discard(char c) { resync_ = !(c == '\r' || c == '\n'); }

  private:
    enum class State : uint8_t { Collect, Skip, Ready };
    char buf_[kMaxLen + 1] = {};
    uint8_t len_ = 0;
    State state_ = State::Collect;
    bool resync_ = false;
};

}  // namespace sm
