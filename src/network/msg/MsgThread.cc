// Message-loop worker thread implementation.

#include "network/msg/MsgThread.h"

#include <unistd.h>

#include "util/Log.h"

namespace cosmo {

static constexpr int kMsgThreadStop = -99;

void MsgThread::Stop() {
    if (is_exit_)
        return;

    is_exit_ = true;

    // Wake up the worker thread if it is blocked waiting for messages.
    MsgEnvelope msg(kMsgThreadStop, nullptr);
    this->Put(std::move(msg));

    Thread::stop();

    // Drain remaining messages.
    ClearMsgList();

    // Let subclass release its own resources.
    close();
}

size_t MsgThread::Put(MsgEnvelope&& msg, bool push_back) {
    std::unique_lock<std::mutex> lck(mtx_);

    if (msg_size_ < max_count_) {
        if (push_back)
            msg_list_.emplace_back(std::move(msg));
        else
            msg_list_.emplace_front(std::move(msg));

        msg_size_ = msg_list_.size();

        if (1 == msg_size_ || is_wait_) {
            cond_.notify_all();
        }

    }
    // Queue full — discard message.
    else {
        ClearMsg(msg);
    }

    return msg_size_;
}

size_t MsgThread::MsgSize() const {
    return msg_size_;
}

bool MsgThread::IsHighLevel() const {
    return msg_size_ > max_count_ / 3;
}

size_t MsgThread::Get(MsgEnvelope& msg) {
    std::unique_lock<std::mutex> lck(mtx_);

    while (0 == msg_size_) {
        is_wait_ = true;
        cond_.wait(lck);
    }

    is_wait_ = false;
    msg      = std::move(msg_list_.front());

    msg_list_.pop_front();

    msg_size_ = msg_list_.size();

    return msg_size_;
}

void MsgThread::run() {
    is_exit_ = false;

    while (!is_exit_) {
        try {
            MsgEnvelope msg;

            // Block until a message is available.
            Get(msg);

            // Poison-pill: exit the loop.
            if (kMsgThreadStop == msg.GetMsgId()) {
                break;
            }

            // Dispatch to subclass handler.
            HandleMsg(msg);
        } catch (const std::exception& ex) {
            fprintf(stderr, "HandleMsg error: [%s]\n", ex.what());
            LOG_WARN("HandleMsg error: {}", ex.what());
        }
    }
}

void MsgThread::ClearMsg(MsgEnvelope& /*msg*/) {
    // unique_ptr in MsgEnvelope handles cleanup automatically.
    // Subclasses may override for additional resource release.
}

void MsgThread::ClearMsgList() {
    std::unique_lock<std::mutex> lck(mtx_);
    for (auto& it : msg_list_) {
        ClearMsg(it);
    }

    msg_list_.clear();
}
}  // namespace cosmo
