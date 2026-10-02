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
    // Inline: Runtime::pump calls it for every byte it reads.
    inline bool feed(char c);
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

inline bool LineReader::feed(char c) {
    if (state_ == State::Ready)
        return false;
    if (c == '\r' || c == '\n') {
        if (state_ == State::Collect && len_ >= 4) {
            buf_[len_] = '\0';
            state_ = State::Ready;
            return true;
        }
        state_ = State::Collect;
        len_ = 0;
        return false;
    }
    if (state_ == State::Skip)
        return false;
    // A noise byte before a line starts (the WCB's TX glitching when it resets) is ignored, so it
    // can't cost the next request. The line that follows still has to start "@SM " and pass its CRC.
    if (len_ == 0 && (c < 0x20 || c > 0x7E))
        return false;
    // Noise, an over-long line or anything that isn't "@SM " is dropped up to the next line end.
    if (c < 0x20 || c > 0x7E || len_ >= kMaxLen || (len_ < 4 && c != "@SM "[len_])) {
        state_ = State::Skip;
        len_ = 0;
        return false;
    }
    buf_[len_++] = c;
    return false;
}

}  // namespace sm
