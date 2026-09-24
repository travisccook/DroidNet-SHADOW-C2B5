#include "sm_line.h"

namespace sm {

namespace {
const char kMarker[] = "@SM ";
constexpr uint8_t kMarkerLen = 4;
}  // namespace

bool LineReader::feed(char c) {
    if (state_ == State::Ready)
        return false;
    if (c == '\r' || c == '\n') {
        if (state_ == State::Collect && len_ >= kMarkerLen) {
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
    // Noise, an over-long line or anything that isn't "@SM " is dropped up to the next line end.
    if (c < 0x20 || c > 0x7E || len_ >= kMaxLen || (len_ < kMarkerLen && c != kMarker[len_])) {
        state_ = State::Skip;
        len_ = 0;
        return false;
    }
    buf_[len_++] = c;
    return false;
}

void LineReader::consume() {
    state_ = State::Collect;
    len_ = 0;
    buf_[0] = '\0';
}

}  // namespace sm
