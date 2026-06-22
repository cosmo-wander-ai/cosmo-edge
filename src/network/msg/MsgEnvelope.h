// Message envelope for the actor-style message thread pipeline.

#pragma once

#include <list>
#include <memory>

#include "network/msg/MsgTask.h"

namespace cosmo {

class MsgEnvelope {
public:
    MsgEnvelope() : msg_id_(0) {}

    MsgEnvelope(int msg_id, std::unique_ptr<MsgTask> data) : msg_id_(msg_id), data_(std::move(data)) {}

    // Move-only: prevent shallow-copy of the owned pointer
    MsgEnvelope(const MsgEnvelope&)            = delete;
    MsgEnvelope& operator=(const MsgEnvelope&) = delete;

    MsgEnvelope(MsgEnvelope&& other) noexcept : msg_id_(other.msg_id_), data_(std::move(other.data_)) {
        other.msg_id_ = 0;
    }

    MsgEnvelope& operator=(MsgEnvelope&& other) noexcept {
        if (this != &other) {
            msg_id_       = other.msg_id_;
            data_         = std::move(other.data_);
            other.msg_id_ = 0;
        }
        return *this;
    }

    virtual ~MsgEnvelope() = default;

    int GetMsgId() const {
        return msg_id_;
    }

    // Returns the owned task pointer without releasing ownership.
    // Caller must NOT delete the returned pointer.
    MsgTask* GetData() const {
        return data_.get();
    }

    // Releases ownership of the task pointer.
    std::unique_ptr<MsgTask> ReleaseData() {
        return std::move(data_);
    }

private:
    int msg_id_;                     // Message ID (dispatching key)
    std::unique_ptr<MsgTask> data_;  // Payload — exclusive ownership
};

// Message list used by MsgThread
using MsgEnvelopeList = std::list<MsgEnvelope>;

}  // namespace cosmo
