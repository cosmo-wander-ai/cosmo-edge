#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace cosmo {
// Internal one-command rendezvous. The worker creates and destroys Prepared;
// no SDK owner may be created in the requester's callback captures. Preparing
// must not publish registration; only Commit may publish it. This is not an
// external API, deployment authority, or model/witness admission decision.
class NativeFaceWorkerInit {
public:
    class Prepared {
    public:
        virtual ~Prepared()   = default;
        virtual bool Commit() = 0;
    };
    using Factory = std::function<std::unique_ptr<Prepared>()>;

    bool Request(Factory factory, std::chrono::milliseconds timeout) {
        if (!factory || timeout <= std::chrono::milliseconds::zero())
            return false;
        auto command      = std::make_shared<Command>();
        command->factory  = std::move(factory);
        command->deadline = std::chrono::steady_clock::now() + timeout;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || pending_ || active_)
                return false;
            pending_ = command;
        }
        std::unique_lock<std::mutex> lock(command->mutex);
        if (!command->condition.wait_until(lock, command->deadline, [&] { return command->done; })) {
            command->cancelled = true;
            return false;
        }
        return command->result;
    }

    // Called only on the detector worker. It is safe to poll without a command.
    bool PumpOnce() {
        std::shared_ptr<Command> command;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || !pending_)
                return false;
            if (worker_ == std::thread::id{})
                worker_ = std::this_thread::get_id();
            if (worker_ != std::this_thread::get_id())
                return false;
            command = std::move(pending_);
            active_ = command;
        }
        bool prepare = false;
        {
            std::lock_guard<std::mutex> lock(command->mutex);
            prepare = !command->cancelled && std::chrono::steady_clock::now() < command->deadline;
        }
        std::unique_ptr<Prepared> prepared;
        if (prepare) {
            try {
                prepared = command->factory();
            } catch (...) {
            }
        }
        {
            // Serialize commit with Stop: whichever acquires the mailbox lock
            // first establishes whether this transaction may publish.
            std::lock_guard<std::mutex> mailbox_lock(mutex_);
            std::lock_guard<std::mutex> lock(command->mutex);
            if (prepared && !stopped_ && !command->cancelled &&
                std::chrono::steady_clock::now() < command->deadline) {
                try {
                    command->result = prepared->Commit();
                } catch (...) {
                    command->result = false;
                }
            }
            command->done = true;
        }
        command->condition.notify_all();
        // prepared is destroyed on the worker even if requester timed out.
        prepared.reset();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.reset();
        }
        return true;
    }

    void Stop() {
        std::shared_ptr<Command> pending, active;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            pending  = std::move(pending_);
            active   = active_;
        }
        Cancel(pending);
        Cancel(active);
    }
    bool Reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_ || active_)
            return false;
        stopped_ = false;
        worker_  = {};
        return true;
    }

private:
    struct Command {
        std::mutex mutex;
        std::condition_variable condition;
        Factory factory;
        std::chrono::steady_clock::time_point deadline;
        bool cancelled{false};
        bool done{false};
        bool result{false};
    };
    static void Cancel(const std::shared_ptr<Command>& command) {
        if (!command)
            return;
        {
            std::lock_guard<std::mutex> lock(command->mutex);
            command->cancelled = true;
            command->done      = true;
            command->result    = false;
        }
        command->condition.notify_all();
    }
    std::mutex mutex_;
    std::shared_ptr<Command> pending_;
    std::shared_ptr<Command> active_;
    std::thread::id worker_;
    bool stopped_{false};
};
}  // namespace cosmo
