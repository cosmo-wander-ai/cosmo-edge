#pragma once

#include "nn/core/macros.h"

namespace cosmo::nn {

/**
 * @brief Profiler
 * @note Profiler is a interface for profiling.
 * @note It is used to report the time of each node.
 */
class PUBLIC IProfiler {
public:
    virtual ~IProfiler() noexcept {}

    /**
     * @brief Report the time of each node
     *
     * @param node_name The name of node
     * @param time The time of node inference
     */
    virtual void ReportNodeTime(const char* node_name, double time) = 0;

    /**
     * @brief Report the graph info
     *
     * @param msg The info of graph
     */
    virtual void ReportGraphInfo(const char* msg) = 0;
};

}  // namespace cosmo::nn
