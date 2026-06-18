// Base class for all message tasks (polymorphic payload in MsgEnvelope).

#pragma once

namespace cosmo {

// Base class for all message tasks. Subclasses carry domain-specific data
// and are transported via MsgEnvelope through the message thread pipeline.
struct MsgTask {
    MsgTask()          = default;
    virtual ~MsgTask() = default;
};

}  // namespace cosmo
