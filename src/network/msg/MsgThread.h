// Message-loop worker thread (producer/consumer model).

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "network/msg/MsgEnvelope.h"
#include "network/msg/MsgTask.h"
#include "util/Thread.h"

namespace cosmo {

class MsgThread : public util::Thread {
public:
    MsgThread(const std::string& name, size_t maxCount = 0xffff)
        : Thread("MsgThread " + name), max_count_(maxCount), msg_size_(0) {}

    virtual ~MsgThread() = default;

    // Stop the message-loop thread and join.
    void Stop();

    // Enqueue a message. Returns current queue size.
    size_t Put(MsgEnvelope&& msg, bool push_back = true);

    // Current pending message count.
    size_t MsgSize() const;

    // Returns true when queue exceeds 1/3 of capacity.
    bool IsHighLevel() const;

protected:
    // Subclasses must implement this to process each message.
    virtual void HandleMsg(MsgEnvelope& msg) = 0;

    // Worker thread entry point.
    void run() override;

    // Hook called after stop, before destruction. Override to release resources.
    virtual void close() {}

    // Hook to release a single message's resources. Default is no-op
    // since unique_ptr in MsgEnvelope handles cleanup automatically.
    virtual void ClearMsg(MsgEnvelope& msg);

    // Drain and release all pending messages before shutdown.
    virtual void ClearMsgList();

private:
    // Dequeue the front message (blocks until available).
    size_t Get(MsgEnvelope& msg);

protected:
    MsgEnvelopeList msg_list_;  // Pending message queue

    size_t max_count_;  // Max queue capacity
    size_t msg_size_;   // Cached queue size

    std::atomic<bool> is_exit_{false};  // Shutdown flag (cross-thread)
    std::atomic<bool> is_wait_{false};  // Whether worker is waiting on condvar

    std::mutex mtx_;
    std::condition_variable cond_;
};

}  // namespace cosmo
