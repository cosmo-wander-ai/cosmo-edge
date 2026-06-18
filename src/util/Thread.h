// Base class for named, joinable worker threads with registry integration.

#pragma once

#include <mutex>
#include <string>
#include <thread>

namespace cosmo::util {

class Thread {
public:
    explicit Thread(std::string name);
    virtual ~Thread();

    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&)                 = delete;
    Thread& operator=(Thread&&)      = delete;

    // Start the worker thread. Returns false if a previous run is still joinable.
    bool start();

    // Block until the worker thread finishes (joins).
    void stop();

    const std::string& Name() const;

protected:
    // Subclasses implement the thread body here.
    virtual void run() = 0;

private:
    std::mutex lifecycle_mtx_;
    std::thread thread_;
    std::string uuid_;
    std::string name_;
};

}  // namespace cosmo::util
