#pragma once

#include <chrono>

namespace cosmo::nn {

/**
 * @brief Timer
 *  Timer is a simple timer class.
 *  It can be used to measure the time spent in a scope.
 *  It can also be used to measure the time spent in a function.
 *
 *  Example:
 *  ```cpp
 *  Timer timer;
 *  timer.Start();
 *  // do something
 *  timer.Stop();
 *  double time = timer.GetTime();
 *  ```
 *
 */
class Timer {
public:
    /**
     * @return Returns the time in microseconds
     */
    double GetTime();
    void Reset();
    void Start();
    void Stop();

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> begin;
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
};

}  // namespace cosmo::nn
