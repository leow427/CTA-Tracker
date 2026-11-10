#pragma once
// The send task's order of work for each drawn strip (main.cpp's sendTask): hash its rows, send the runs that changed,
// and give the strip's buffer back to the loop once its last run is on the panel. Plain C++, so tools/frame_test.cpp
// runs it on a laptop against a model of the DMA and the loop.
//
// There are two strip buffers: the loop draws into one while the sender works on the other. A strip's last run may
// still be going out by DMA when the sender has finished with it, so the sender holds that buffer and gives it back
// at the first of: the next strip's first run (whose rows were hashed while it went out), the end of the frame, or
// the sender running out of jobs (the loop is still drawing, so it gets the buffer as soon as possible).
#include "row_hash.h"

// finish(): waits until every send started is on the panel. give(buf): hands strip buffer buf back to the loop.
template <typename Finish, typename Give>
class StripSender {
public:
    StripSender(Finish finish, Give give) : finish_(finish), give_(give) {}

    // Before waiting for the next job; jobWaiting: one is already queued.
    void beforeWait(bool jobWaiting) {
        if (held_ >= 0 && !jobWaiting) settle();
    }

    // A drawn strip in buffer buf (n panel rows of `rows` pixels, as changedRuns). send(r0, r1) starts sending one run;
    // each waits first for the run before it, this strip's or the last strip's, which also frees the last strip's
    // buffer. A strip with nothing to send goes straight back.
    template <typename Send>
    void strip(const uint16_t* px, int rows, int n, uint32_t* hashes, bool all, int mergeGap, int buf, Send&& send) {
        bool sent = false;
        changedRuns(px, rows, n, hashes, all, mergeGap, [&](int r0, int r1) {
            settle();
            send(r0, r1);
            sent = true;
        });
        if (sent) held_ = buf;
        else give_(buf);
    }

    // The end of a frame: all of it on the panel, and both buffers back.
    void frameEnd() { settle(); }

private:
    void settle() {
        finish_();
        if (held_ >= 0) {
            give_(held_);
            held_ = -1;
        }
    }

    Finish finish_;
    Give give_;
    int held_ = -1;   // a strip buffer whose last run may still be going out
};
