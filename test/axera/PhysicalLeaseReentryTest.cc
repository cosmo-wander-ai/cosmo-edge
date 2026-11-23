#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <vector>

#include "nn/device/axera/axera_physical_lease.h"

using namespace cosmo::nn;
using Mode = AxPhysicalAccess::Mode;
struct Holder {
    std::shared_ptr<AxPhysicalLease> nested;
};
struct Reenter {
    int* calls;
    int* successful;
    ~Reenter() {
        ++*calls;
        AxPhysicalLease fresh;
        if (fresh.Acquire({{0x90000, 16, Mode::Write}}, {std::make_shared<int>(7)}))
            ++*successful;
    }
};

void RunTests() {
    auto marker = std::make_shared<int>(1);
    {
        auto inner = std::make_shared<AxPhysicalLease>();
        assert(inner->Acquire({{0x20000, 16, Mode::Write}}, {marker}));
        auto owner    = std::make_shared<Holder>();
        owner->nested = inner;
        auto outer    = std::make_unique<AxPhysicalLease>();
        assert(outer->Acquire({{0x10000, 16, Mode::Write}}, {owner}));
        owner.reset();
        inner.reset();
        outer.reset();  // owner releases nested lease during destruction
        AxPhysicalLease probe;
        assert(probe.Acquire({{0x20000, 16, Mode::Write}}, {marker}));
    }
    {
        int calls = 0, successful = 0;
        auto owner        = std::make_shared<Reenter>();
        owner->calls      = &calls;
        owner->successful = &successful;
        auto lease        = std::make_unique<AxPhysicalLease>();
        assert(lease->Acquire({{0x30000, 16, Mode::Write}}, {owner}));
        owner.reset();
        lease.reset();
        assert(calls == 1 && successful == 1);
    }
    {
        int calls = 0, successful = 0;
        AxPhysicalLease blocker;
        assert(blocker.Acquire({{0x40000, 16, Mode::Write}}, {marker}));
        auto owner        = std::make_shared<Reenter>();
        owner->calls      = &calls;
        owner->successful = &successful;
        AxPhysicalLease denied;
        std::vector<std::shared_ptr<const void>> owners;
        owners.emplace_back(std::move(owner));
        // Candidate rejected while registry is locked; its owner destruction
        // must also be deferred until the lock has been released.
        assert(!denied.Acquire({{0x40000, 16, Mode::Read}}, std::move(owners)));
        assert(calls == 1 && successful == 1);
    }
    {
        auto owner              = std::make_shared<int>(9);
        std::weak_ptr<int> weak = owner;
        AxPhysicalLease uncertain;
        assert(uncertain.Acquire({{0x50000, 16, Mode::Read}}, {owner}));
        uncertain.Quarantine();
        owner.reset();
        assert(!weak.expired());
        AxPhysicalLease other;
        assert(!other.Acquire({{0x50000, 16, Mode::Read}}, {marker}));
    }
}

int main() {
    // A deadlock terminates the child and fails the parent rather than hanging
    // the test suite. No process threads exist when fork is called.
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        alarm(3);
        RunTests();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
