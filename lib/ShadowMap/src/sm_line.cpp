#include "sm_line.h"

namespace sm {

void LineReader::consume() {
    state_ = resync_ ? State::Skip : State::Collect;
    resync_ = false;
    len_ = 0;
    buf_[0] = '\0';
}

}  // namespace sm
