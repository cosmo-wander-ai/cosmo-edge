#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <memory>
#include <type_traits>
#include <vector>

#include "media/AxeraCompressedPacketLease.h"
#include "nn/device/axera/axera_physical_lease.h"

using namespace cosmo::media;

template <class T, class = void>
struct HasSubmit : std::false_type {};
template <class T>
struct HasSubmit<T, std::void_t<decltype(std::declval<T&>().MockSubmit(true))>> : std::true_type {};

AxPacketLocation Unit(int64_t offset, size_t size, int64_t pts, int64_t dts, uint64_t seq) {
    return {offset, size, pts, dts, seq, AxPacketEncoding::Avcc, 1, 12288, 512, seq == 0};
}
AxPacketMemory Memory(uint64_t physical, const std::shared_ptr<std::vector<uint8_t>>& bytes) {
    return {physical, bytes->data(), bytes->size(), bytes};
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
#ifndef COSMO_AXERA_PACKET_MOCK
    static_assert(!HasSubmit<AxPacketLease>::value, "production must not expose an unverified submit");
    auto bytes = std::make_shared<std::vector<uint8_t>>(330947);
    AxPacketPool pool({Memory(0x100000, bytes)});
    auto a = pool.Acquire();
    assert(pool.Valid() && a && a->ReadFromFile(fd, Unit(20532, 330947, 0, -2560, 0)));
    assert(a->MappedAddress()[3] == 2 && a->PhysicalAddress() == 0x100000);
#else
    static_assert(HasSubmit<AxPacketLease>::value, "mock transition unavailable");
    {
        auto first  = std::make_shared<std::vector<uint8_t>>(330947);
        auto second = std::make_shared<std::vector<uint8_t>>(330947);
        AxPacketPool pool({Memory(0x100000, first), Memory(0x200000, second)});
        assert(pool.Valid());
        AxPacketPool aliased({Memory(0x100010, first)});
        auto distinct = std::make_shared<std::vector<uint8_t>>(16);
        AxPacketPool same_physical({Memory(0x100010, distinct)});
        auto a = pool.Acquire(), b = pool.Acquire();
        assert(a && b && !pool.Acquire());
        assert(aliased.Valid() && !aliased.Acquire());
        assert(same_physical.Valid() && !same_physical.Acquire());
        cosmo::nn::AxPhysicalLease ivps;
        assert(!ivps.Acquire({{0x100000, 12, cosmo::nn::AxPhysicalAccess::Mode::Read}}, {first}));
        cosmo::nn::AxPhysicalLease nn_output;
        assert(!nn_output.Acquire({{0x100000, 12, cosmo::nn::AxPhysicalAccess::Mode::Write}}, {first}));
        assert(!b->ReadFromFile(fd, Unit(351479, 191482, 1536, -2048, 1)));
        assert(a->ReadFromFile(fd, Unit(20532, 330947, 0, -2560, 0)));
        const int other_fd = open(argv[0], O_RDONLY | O_CLOEXEC);
        assert(other_fd >= 0);
        assert(!b->ReadFromFile(other_fd, Unit(0, 4, 1536, -2048, 1)));
        close(other_fd);
        assert(b->ReadFromFile(fd, Unit(351479, 191482, 1536, -2048, 1)));
        assert(a->MappedAddress()[0] == 0 && a->MappedAddress()[3] == 2 && a->MappedAddress()[4] == 9 &&
               a->MappedAddress()[9] == 0x19);
        assert(a->PhysicalAddress() == 0x100000 && a->Location().pts == 0 && b->Location().pts == 1536 &&
               b->Location().dts == -2048 && a->Location().time_base_den == 12288 && a->Location().keyframe &&
               !b->Location().keyframe);
        assert(!b->MockSubmit(true));     // preserve decode order
        assert(!pool.MockEndOfStream());  // ready packets cannot be skipped
        assert(a->MockSubmit(true) && b->MockSubmit(true));
        assert(a->MockComplete());
        auto c = pool.Acquire();
        assert(c && c->ReadFromFile(fd, Unit(542961, 12426, 512, -1536, 2)));
        assert(c->Location().pts < b->Location().pts);  // B-frame PTS reorders
        assert(c->MockSubmit(true));
        assert(pool.MockEndOfStream());
        assert(!pool.MockEndOfStream() && !pool.Acquire());
        assert(c->MockComplete() && b->MockComplete());  // completion order is independent
    }
    {
        auto storage                             = std::make_shared<std::vector<uint8_t>>(330947);
        std::weak_ptr<std::vector<uint8_t>> weak = storage;
        {
            AxPacketPool pool({Memory(0x300000, storage)});
            auto a = pool.Acquire();
            assert(a && a->ReadFromFile(fd, Unit(20532, 330947, 0, -2560, 0)));
            assert(!a->MockSubmit(false));  // uncertain failure permanently quarantined
            assert(!a->MockComplete() && !pool.MockEndOfStream() && !pool.Valid());
        }
        auto aliased_mem = Memory(0x300000, storage);
        storage.reset();
        assert(!weak.expired());
        AxPacketPool alias({aliased_mem});
        assert(alias.Valid() && !alias.Acquire());
        cosmo::nn::AxPhysicalLease ivps_after_failure;
        assert(!ivps_after_failure.Acquire({{0x300000, 4, cosmo::nn::AxPhysicalAccess::Mode::Read}},
                                           {weak.lock()}));
        aliased_mem.owner.reset();
        assert(!weak.expired());
    }
    {
        auto storage                             = std::make_shared<std::vector<uint8_t>>(330947);
        std::weak_ptr<std::vector<uint8_t>> weak = storage;
        {
            AxPacketPool pool({Memory(0x400000, storage)});
            auto a = pool.Acquire();
            assert(a && a->ReadFromFile(fd, Unit(20532, 330947, 0, -2560, 0)));
            assert(a->MockSubmit(true));
            a.reset();  // no completion proof: quarantine, never recycle
            assert(!pool.Valid());
        }
        storage.reset();
        assert(!weak.expired());
    }
    {
        auto bytes = std::make_shared<std::vector<uint8_t>>(16);
        AxPacketPool pool({Memory(0x500000, bytes)});
        auto a = pool.Acquire();
        assert(a && !a->ReadFromFile(fd, Unit(20532, 17, 0, -1, 0)));
        int pipes[2];
        assert(pipe(pipes) == 0);
        assert(!a->ReadFromFile(pipes[0], Unit(0, 4, 0, -1, 0)));
        close(pipes[0]);
        close(pipes[1]);
        assert(!a->ReadFromFile(fd, Unit(INT64_MAX - 2, 8, 0, -1, 0)));
        assert(!a->ReadFromFile(fd, Unit(20532, 4, 0, -1, 1)));
        assert(a->ReadFromFile(fd, Unit(20532, 4, 0, -1, 0)));
        a.reset();
        assert(!pool.Valid());  // published packet was abandoned, no EOS success
    }
#endif
    close(fd);
}
