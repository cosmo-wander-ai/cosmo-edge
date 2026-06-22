#include "nn/utils/timer.h"

namespace cosmo::nn {

void Timer::Reset() {
    begin = end = std::chrono::high_resolution_clock::now();
}

void Timer::Start() {
    begin = std::chrono::high_resolution_clock::now();
}

void Timer::Stop() {
    end = std::chrono::high_resolution_clock::now();
}

double Timer::GetTime() {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
}

}  // namespace cosmo::nn