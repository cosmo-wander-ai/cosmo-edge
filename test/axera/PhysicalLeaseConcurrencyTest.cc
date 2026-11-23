#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>

#include "nn/device/axera/axera_physical_lease.h"

using namespace cosmo::nn;
using Mode = AxPhysicalAccess::Mode;

template <class A, class B>
void Race(A a, B b) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    auto start = [&](auto fn) {
        ++ready;
        while (!go.load())
            std::this_thread::yield();
        fn();
    };
    std::thread left(start, a), right(start, b);
    while (ready.load() != 2)
        std::this_thread::yield();
    go = true;
    left.join();
    right.join();
}

int main() {
    auto owner = std::make_shared<int>(1);
    {
        AxPhysicalLease terminal;
        terminal.Quarantine();
        assert(!terminal.Acquire({{0x10000, 16, Mode::Write}}, {owner}));
    }
    for (unsigned i = 0; i < 256; ++i) {
        const uint64_t a = 0x100000 + i * 0x1000;
        const uint64_t b = a + 0x100;
        auto shared      = std::make_unique<AxPhysicalLease>();
        std::atomic<bool> first{false}, second{false};
        Race([&] { first = shared->Acquire({{a, 16, Mode::Write}}, {owner}); },
             [&] { second = shared->Acquire({{b, 16, Mode::Write}}, {owner}); });
        assert(first.load() != second.load());
        AxPhysicalLease winner_probe, loser_probe;
        const auto winner = first ? a : b;
        const auto loser  = first ? b : a;
        assert(!winner_probe.Acquire({{winner, 16, Mode::Read}}, {owner}));
        assert(loser_probe.Acquire({{loser, 16, Mode::Write}}, {owner}));
        shared.reset();
        assert(winner_probe.Acquire({{winner, 16, Mode::Write}}, {owner}));
    }
    for (unsigned i = 0; i < 128; ++i) {
        const uint64_t addr = 0x1000000 + i * 0x100;
        AxPhysicalLease shared;
        std::atomic<bool> acquired{false};
        Race([&] { acquired = shared.Acquire({{addr, 16, Mode::Write}}, {owner}); },
             [&] { shared.Quarantine(); });
        assert(!shared.Acquire({{addr + 0x20, 16, Mode::Write}}, {owner}));  // terminal
        AxPhysicalLease probe;
        const bool available = probe.Acquire({{addr, 16, Mode::Read}}, {owner});
        assert(available != acquired.load());
    }
}
