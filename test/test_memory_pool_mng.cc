#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mem/Allocator.h"
#include "mem/MemoryPoolMng.h"

namespace cosmo::mem {
namespace {

    using namespace std::chrono_literals;

    struct AllocationState {
        struct Storage {
            explicit Storage(size_t size) : bytes(size) {
                block.size = size;
                block.data = bytes.data();
            }
            std::vector<uint8_t> bytes;
            Block block;
        };

        std::mutex mutex;
        std::condition_variable changed;
        bool fail            = false;
        size_t allocated     = 0;
        size_t freed         = 0;
        size_t unknown_frees = 0;
        std::map<size_t, size_t> calls;
        std::map<Block*, std::unique_ptr<Storage>> live;

        bool WaitForCalls(size_t size, size_t count) {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, 3s, [&] { return calls[size] >= count; });
        }
    };

    class ControlledAllocator final : public Allocator {
    public:
        explicit ControlledAllocator(std::shared_ptr<AllocationState> state) : state_(std::move(state)) {}

        Block* Allocate(size_t size) override {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->calls[size];
            Block* result = nullptr;
            if (!state_->fail) {
                auto storage = std::make_unique<AllocationState::Storage>(size);
                result       = &storage->block;
                state_->live.emplace(result, std::move(storage));
                ++state_->allocated;
            }
            state_->changed.notify_all();
            return result;
        }

        void Free(Block* block) override {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->live.erase(block) == 1) {
                ++state_->freed;
            } else {
                ++state_->unknown_frees;
            }
            state_->changed.notify_all();
        }

    private:
        std::shared_ptr<AllocationState> state_;
    };

    // Keep borrowed blocks valid through assertions, including failed REQUIREs.
    class BorrowedBlocks {
    public:
        explicit BorrowedBlocks(MemoryPoolMng& pool) : pool_(pool) {}
        ~BorrowedBlocks() {
            for (auto* block : blocks) {
                pool_.Recycle(block);
            }
        }
        std::vector<Block*> blocks;

    private:
        MemoryPoolMng& pool_;
    };

    bool WaitForIdle(MemoryPoolMng& pool, size_t size, size_t count) {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        do {
            for (const auto& status : pool.Status()) {
                if (status.pool_size == size && status.idle_cnt >= count) {
                    return true;
                }
            }
            std::this_thread::sleep_for(1ms);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    TEST_CASE("Memory pool retries after allocation failure and preserves status", "[memory-pool-mng]") {
        auto state  = std::make_shared<AllocationState>();
        state->fail = true;
        {
            MemoryPoolMng pool(std::make_unique<ControlledAllocator>(state), {1024});
            BorrowedBlocks borrowed(pool);
            REQUIRE(pool.Acquire(100) == nullptr);
            REQUIRE(state->WaitForCalls(1024, 1));
            const auto empty = pool.Status();
            REQUIRE(empty.size() == 1);
            REQUIRE(empty[0].pool_size == 1024);
            REQUIRE(empty[0].used_cnt == 0);
            REQUIRE(empty[0].idle_cnt == 0);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                REQUIRE(state->allocated == 0);
                state->fail = false;
            }
            REQUIRE(pool.Acquire(100) == nullptr);
            REQUIRE(state->WaitForCalls(1024, 21));
            REQUIRE(WaitForIdle(pool, 1024, 20));
            auto* block = pool.Acquire(100);
            if (block) {
                borrowed.blocks.push_back(block);
            }
            REQUIRE(block != nullptr);
            REQUIRE(block->size == 1024);
            const auto in_use = pool.Status();
            REQUIRE(in_use[0].used_cnt == 1);
            REQUIRE(in_use[0].idle_cnt == 19);
            REQUIRE(in_use[0].used_nodes_status.size() == 1);
            REQUIRE_FALSE(pool.OutputMallocBuf().empty());
            pool.Recycle(block);
            borrowed.blocks.clear();
            REQUIRE(pool.Status()[0].idle_cnt == 20);
            REQUIRE(pool.Status()[0].used_cnt == 0);
            REQUIRE_NOTHROW(pool.Recycle(nullptr));
        }
        REQUIRE(state->allocated == 20);
        REQUIRE(state->freed == state->allocated);
        REQUIRE(state->unknown_frees == 0);
        REQUIRE(state->live.empty());
    }

    TEST_CASE("Memory pool capacity rejects growth and recycled blocks remain reusable",
              "[memory-pool-mng]") {
        auto state = std::make_shared<AllocationState>();
        {
            MemoryPoolMng pool(std::make_unique<ControlledAllocator>(state), {1024, 2048});
            BorrowedBlocks borrowed(pool);
            REQUIRE(pool.Acquire(1024) == nullptr);
            REQUIRE(state->WaitForCalls(1024, 20));
            REQUIRE(WaitForIdle(pool, 1024, 20));
            for (size_t index = 0; index < 80; ++index) {
                REQUIRE(WaitForIdle(pool, 1024, 1));
                auto* block = pool.Acquire(1024);
                if (block) {
                    borrowed.blocks.push_back(block);
                }
                REQUIRE(block != nullptr);
            }
            REQUIRE(pool.Acquire(1024) == nullptr);

            // A second bucket's allocation acknowledges earlier queued capacity checks.
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            bool marker_started = false;
            do {
                auto* marker = pool.Acquire(2048);
                if (marker) {
                    borrowed.blocks.push_back(marker);
                }
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    marker_started = state->calls[2048] > 0;
                }
                if (!marker_started) {
                    std::this_thread::sleep_for(1ms);
                }
            } while (!marker_started && std::chrono::steady_clock::now() < deadline);
            REQUIRE(marker_started);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                REQUIRE(state->calls[1024] == 80);
            }
            const auto full = pool.Status();
            REQUIRE(full[0].pool_size == 1024);
            REQUIRE(full[0].used_cnt == 80);
            REQUIRE(full[0].idle_cnt == 0);
            REQUIRE(full[0].used_nodes_status.size() == 80);

            auto* returned = borrowed.blocks.front();
            pool.Recycle(returned);
            borrowed.blocks.erase(borrowed.blocks.begin());
            auto* reused = pool.Acquire(1024);
            if (reused) {
                borrowed.blocks.push_back(reused);
            }
            REQUIRE(reused == returned);
        }
        REQUIRE(state->freed == state->allocated);
        REQUIRE(state->unknown_frees == 0);
        REQUIRE(state->live.empty());
    }

}  // namespace
}  // namespace cosmo::mem
