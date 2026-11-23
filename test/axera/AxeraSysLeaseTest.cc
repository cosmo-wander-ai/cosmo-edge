#include <atomic>
#include <cassert>
#include <thread>
#include <utility>
#include <vector>

#include "nn/device/axera/axera_sys.h"

void* AcquirePeerLease();
void ReleasePeerLease(void* lease);

namespace {
std::atomic<int> init_calls{0};
std::atomic<int> deinit_calls{0};
std::atomic<bool> fail_first{true};
}  // namespace

extern "C" int AX_SYS_Init() {
    ++init_calls;
    return fail_first.exchange(false) ? 17 : 0;
}

extern "C" int AX_SYS_Deinit() {
    ++deinit_calls;
    return 0;
}

int main() {
    using cosmo::nn::AxeraSysLease;
    AxeraSysLease owner;
    assert(owner.Acquire() == 17);
    assert(!owner.Held());
    assert(owner.Acquire() == 0);
    assert(owner.Acquire() == 0);
    assert(init_calls == 2);

    void* peer = AcquirePeerLease();
    assert(peer != nullptr && init_calls == 2);
    ReleasePeerLease(peer);
    assert(deinit_calls == 0);

    AxeraSysLease other;
    assert(other.Acquire() == 0);
    AxeraSysLease moved(std::move(other));
    assert(!other.Held() && moved.Held());
    AxeraSysLease assigned;
    assert(assigned.Acquire() == 0);
    assigned = std::move(moved);
    assert(!moved.Held() && assigned.Held());
    assigned.Reset();
    assert(!assigned.Held());
    assigned.Reset();
    assert(deinit_calls == 0);

    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([] {
            for (int j = 0; j < 1000; ++j) {
                AxeraSysLease lease;
                assert(lease.Acquire() == 0);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    assert(init_calls == 2);
    assert(deinit_calls == 0);
    owner.Reset();
    assert(!owner.Held());
    assert(deinit_calls == 1);

    assert(owner.Acquire() == 0);
    owner.Reset();
    assert(!owner.Held());
    assert(init_calls == 3 && deinit_calls == 2);
}
