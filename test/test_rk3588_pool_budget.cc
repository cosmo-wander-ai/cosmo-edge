#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mem/FixedBlockPool.h"
#include "mem/MemoryPoolMng.h"

namespace cosmo::mem {
namespace {

    class ScopedPoolBudgetEnv {
    public:
        ScopedPoolBudgetEnv() {
            const char* original = std::getenv("COSMO_RK3588_I420_POOL_BLOCKS");
            if (original) {
                had_value_ = true;
                original_  = original;
            }
        }
        ~ScopedPoolBudgetEnv() {
            if (had_value_) {
                setenv("COSMO_RK3588_I420_POOL_BLOCKS", original_.c_str(), 1);
            } else {
                unsetenv("COSMO_RK3588_I420_POOL_BLOCKS");
            }
        }

    private:
        bool had_value_ = false;
        std::string original_;
    };

    TEST_CASE("I420 pool constructor applies only the compiled RK3588 override", "[memory-pool-budget]") {
        ScopedPoolBudgetEnv saved;
        constexpr size_t kI420Bucket = 1920 * 1088 * 3 / 2;
        REQUIRE(setenv("COSMO_RK3588_I420_POOL_BLOCKS", "256", 1) == 0);
        FixedBlockPool configured(kI420Bucket);
#ifdef COSMO_RKNN_TARGET_CHIP
        const auto expected = std::string(COSMO_RKNN_TARGET_CHIP) == "rk3588" ? 256U : 200U;
#else
        const auto expected = 200U;
#endif
        REQUIRE(configured.GetMaxLimit() == expected);
        FixedBlockPool other_bucket(1024);
        REQUIRE(other_bucket.GetMaxLimit() == 80);
        REQUIRE(setenv("COSMO_RK3588_I420_POOL_BLOCKS", "invalid", 1) == 0);
        FixedBlockPool invalid(kI420Bucket);
        REQUIRE(invalid.GetMaxLimit() == 200);
        REQUIRE(configured.GetMaxLimit() == expected);  // frozen at construction
    }

    TEST_CASE("RK3588 I420 budget is opt-in and validates input", "[memory-pool-budget]") {
        REQUIRE(ResolveI420PoolLimit(nullptr, "256").blocks == 200);
        REQUIRE(ResolveI420PoolLimit("rk3576", "256").blocks == 200);
        REQUIRE(ResolveI420PoolLimit("rv1126b", "256").blocks == 200);
        REQUIRE_FALSE(ResolveI420PoolLimit("rk3576", "bad").invalid);
        REQUIRE(ResolveI420PoolLimit("rk3588", nullptr).blocks == 200);
        REQUIRE_FALSE(ResolveI420PoolLimit("rk3588", nullptr).invalid);
        for (const char* input :
             {"", "199", "401", "-1", "+256", " 256", "256x", "9999999999999999999999999"}) {
            const auto result = ResolveI420PoolLimit("rk3588", input);
            INFO(input);
            REQUIRE(result.blocks == 200);
            REQUIRE(result.invalid);
        }
        for (const auto expected : {200U, 201U, 256U, 400U}) {
            const auto setting = std::to_string(expected);
            const auto result  = ResolveI420PoolLimit("rk3588", setting.c_str());
            REQUIRE(result.blocks == expected);
            REQUIRE_FALSE(result.invalid);
        }
    }

    struct TestState {
        struct Storage {
            uint8_t byte = 0;
            Block block;
        };
        std::mutex mutex;
        size_t calls     = 0;
        size_t allocated = 0;
        size_t freed     = 0;
        std::map<Block*, std::unique_ptr<Storage>> live;
    };

    // Allocate only a single byte per fake block; no test dereferences its payload.
    class PartialFailureAllocator final : public Allocator {
    public:
        explicit PartialFailureAllocator(std::shared_ptr<TestState> state) : state_(std::move(state)) {}
        Block* Allocate(size_t size) override {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (++state_->calls == 3)
                return nullptr;
            auto storage        = std::make_unique<TestState::Storage>();
            storage->block.size = size;
            storage->block.data = &storage->byte;
            auto* result        = &storage->block;
            state_->live.emplace(result, std::move(storage));
            ++state_->allocated;
            return result;
        }
        void Free(Block* block) override {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->freed += state_->live.erase(block);
        }

    private:
        std::shared_ptr<TestState> state_;
    };

    TEST_CASE("Concurrent pool growth respects hard limit after partial allocation failure",
              "[memory-pool-budget]") {
        using namespace std::chrono_literals;
        auto state = std::make_shared<TestState>();
        {
            MemoryPoolMng pool(std::make_unique<PartialFailureAllocator>(state), {1024});
            std::mutex held_mutex;
            std::vector<Block*> held;
            std::atomic<bool> done{false};
            std::vector<std::thread> threads;
            for (int i = 0; i < 8; ++i) {
                threads.emplace_back([&] {
                    while (!done.load()) {
                        if (auto* block = pool.Acquire(1024)) {
                            std::lock_guard<std::mutex> lock(held_mutex);
                            held.push_back(block);
                        }
                        std::this_thread::sleep_for(1ms);
                    }
                });
            }
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline) {
                {
                    std::lock_guard<std::mutex> lock(held_mutex);
                    if (held.size() >= 80)
                        break;
                }
                std::this_thread::sleep_for(2ms);
            }
            // Continue submitting allocation requests while the pool is full.
            std::this_thread::sleep_for(30ms);
            done = true;
            for (auto& thread : threads)
                thread.join();
            const auto status = pool.Status();
            REQUIRE(status.size() == 1);
            REQUIRE(status[0].used_cnt == 80);
            REQUIRE(status[0].idle_cnt == 0);
            REQUIRE(held.size() == 80);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                REQUIRE(state->allocated == 80);
                REQUIRE(state->calls == 81);  // one injected failure
            }
            // Recycle a single block and verify it can be acquired again at the cap.
            auto* recycled = held.back();
            held.pop_back();
            pool.Recycle(recycled);
            auto* reused = pool.Acquire(1024);
            REQUIRE(reused == recycled);
            held.push_back(reused);
            for (auto* block : held)
                pool.Recycle(block);
        }
        REQUIRE(state->allocated == state->freed);
        REQUIRE(state->live.empty());
    }

}  // namespace
}  // namespace cosmo::mem
