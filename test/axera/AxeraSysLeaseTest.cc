#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <stdexcept>
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
std::atomic<int> deinit_mode{0};
std::atomic<bool> throw_init{false};
}  // namespace

extern "C" int AX_SYS_Init() {
    ++init_calls;
    if (throw_init)
        throw std::runtime_error("fake init exception");
    return fail_first.exchange(false) ? 17 : 0;
}

extern "C" int AX_SYS_Deinit() {
    ++deinit_calls;
    if (deinit_mode == 2)
        throw std::runtime_error("fake deinit failure");
    return deinit_mode == 1 ? -99 : 0;
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
    assert(assigned.Reset());
    assert(assigned.Reset());
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
    assert(owner.Reset());
    assert(deinit_calls == 1);

    assert(owner.Acquire() == 0);
    assert(owner.Reset());
    assert(init_calls == 3 && deinit_calls == 2);

    // Fork isolates sticky process-wide faults, including destructor paths.
    for (int scenario = 1; scenario <= 6; ++scenario) {
        const auto child = fork();
        assert(child >= 0);
        if (child == 0) {
            fail_first               = false;
            const int initial_init   = init_calls;
            const int initial_deinit = deinit_calls;
            deinit_mode              = scenario == 2 ? 2 : 1;
            if (scenario == 1 || scenario == 2) {
                AxeraSysLease lease;
                assert(lease.Acquire() == 0);
                assert(!lease.Reset());
                assert(lease.Held());
                assert(!lease.Reset());
                AxeraSysLease empty;
                lease = std::move(empty);
                assert(lease.Held() && !empty.Held());
                assert(AcquirePeerLease() == nullptr);
            } else if (scenario == 3) {
                void* peer = AcquirePeerLease();
                assert(peer);
                ReleasePeerLease(peer);  // destructor must not throw
                AxeraSysLease later;
                assert(later.Acquire() != 0);
                assert(!later.Held());
            } else if (scenario == 4) {
                AxeraSysLease owner;
                AxeraSysLease other;
                assert(owner.Acquire() == 0 && other.Acquire() == 0);
                assert(other.Reset() && !other.Held());
                assert(deinit_calls == initial_deinit);
                assert(!owner.Reset() && owner.Held());
            } else if (scenario == 5) {
                constexpr int count = 8;
                std::vector<std::unique_ptr<AxeraSysLease>> leases;
                for (int i = 0; i < count; ++i) {
                    auto lease = std::make_unique<AxeraSysLease>();
                    assert(lease->Acquire() == 0);
                    leases.push_back(std::move(lease));
                }
                std::atomic<int> failures{0};
                std::vector<std::thread> workers;
                for (auto& lease : leases) {
                    auto* ptr = lease.get();
                    workers.emplace_back([ptr, &failures] {
                        if (!ptr->Reset())
                            ++failures;
                    });
                }
                for (auto& worker : workers)
                    worker.join();
                assert(failures == 1);
                AxeraSysLease later;
                assert(later.Acquire() != 0);
            } else {
                throw_init = true;
                AxeraSysLease lease;
                assert(lease.Acquire() != 0);
                assert(!lease.Held());
                assert(AcquirePeerLease() == nullptr);
            }
            assert(init_calls == initial_init + 1);
            assert(deinit_calls == initial_deinit + (scenario == 6 ? 0 : 1));
            std::_Exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}
