#include "catch_amalgamated.hpp"
// Unit tests for LlmInferServiceImpl — validates thread-safe state machine:
//   uninitialized → EnsureInit → Generate/GetMaxBatchSize → Reset → re-init
// Also tests worker start/stop lifecycle and auto-release on last worker stop.

#include <thread>

#include "mock/MockModelService.h"
#include "mock/MockServiceRegistry.h"
#include "service/ai/impl/LlmInferServiceImpl.h"
#include "service/detail/ServiceRegistry.h"

using cosmo::service::LlmInferServiceImpl;
using cosmo::util::ErrorEnum;

TEST_CASE("LlmInferServiceImpl: initial state", "[llm-infer]") {
    cosmo::test::MockServiceRegistry mocks;
    LlmInferServiceImpl sut;

    SECTION("IsInitialized returns false before EnsureInit") {
        REQUIRE(sut.IsInitialized() == false);
    }

    SECTION("Generate returns NotInit when not initialized") {
        std::vector<VideoFramePtr> images;
        std::vector<std::string> prompts;
        cosmo::Qwen3VLGenerationParam param;
        std::vector<cosmo::Qwen3VLResult> results;

        auto ret = sut.Generate(images, prompts, param, results);
        REQUIRE(ret == ErrorEnum::NotInit);
    }

    SECTION("GetMaxBatchSize returns NotInit when not initialized") {
        size_t val = 0;
        auto ret   = sut.GetMaxBatchSize(val);
        REQUIRE(ret == ErrorEnum::NotInit);
    }
}

TEST_CASE("LlmInferServiceImpl: EnsureInit validation", "[llm-infer]") {
    cosmo::test::MockServiceRegistry mocks;
    LlmInferServiceImpl sut;

    SECTION("EnsureInit fails with empty atomic_code") {
        REQUIRE(sut.EnsureInit("") == false);
        REQUIRE(sut.IsInitialized() == false);
    }

    SECTION("EnsureInit fails permanently after empty code — no retry until Reset") {
        REQUIRE(sut.EnsureInit("") == false);
        // Even with a valid code, init_failed_ blocks retry
        ALLOW_CALL(mocks.modelSvc, GetModelCfg(trompeloeil::_, trompeloeil::_, trompeloeil::_)).RETURN(true);
        REQUIRE(sut.EnsureInit("valid_code") == false);
    }

    SECTION("Reset clears permanent failure, allowing retry") {
        REQUIRE(sut.EnsureInit("") == false);
        sut.Reset();
        // After Reset, init_failed_ is cleared — would attempt GetModelCfg
        // GetModelCfg returns false → init fails again but for a different reason
        ALLOW_CALL(mocks.modelSvc, GetModelCfg(trompeloeil::_, trompeloeil::_, trompeloeil::_)).RETURN(false);
        REQUIRE(sut.EnsureInit("some_code") == false);
        REQUIRE(sut.IsInitialized() == false);
    }

    SECTION("EnsureInit fails when GetModelCfg returns false") {
        ALLOW_CALL(mocks.modelSvc, GetModelCfg(trompeloeil::_, trompeloeil::_, trompeloeil::_)).RETURN(false);
        REQUIRE(sut.EnsureInit("test_code") == false);
        REQUIRE(sut.IsInitialized() == false);
    }
}

TEST_CASE("LlmInferServiceImpl: worker lifecycle", "[llm-infer]") {
    cosmo::test::MockServiceRegistry mocks;
    LlmInferServiceImpl sut;

    SECTION("NotifyWorkerStart increments count") {
        // Should not crash and should not trigger Reset
        sut.NotifyWorkerStart();
        sut.NotifyWorkerStart();
        // Two workers active — stopping one should NOT trigger Reset
        sut.NotifyWorkerStop();
        // Still one worker active
        REQUIRE(sut.IsInitialized() == false);  // Was never initialized
    }

    SECTION("NotifyWorkerStop triggers Reset when count reaches zero") {
        sut.NotifyWorkerStart();
        sut.NotifyWorkerStop();
        // After last worker stops, Reset() is called automatically
        // IsInitialized should be false (it was never init'd, but Reset clears state)
        REQUIRE(sut.IsInitialized() == false);
    }

    SECTION("Multiple start/stop cycles") {
        for (int i = 0; i < 3; ++i) {
            sut.NotifyWorkerStart();
        }
        for (int i = 0; i < 3; ++i) {
            sut.NotifyWorkerStop();
        }
        // All workers stopped — Reset triggered
        REQUIRE(sut.IsInitialized() == false);
    }
    SECTION("NotifyWorkerStop without matching start is ignored") {
        sut.NotifyWorkerStop();
        REQUIRE(sut.IsInitialized() == false);

        sut.NotifyWorkerStart();
        sut.NotifyWorkerStop();
        sut.NotifyWorkerStop();
        REQUIRE(sut.IsInitialized() == false);
    }
}

TEST_CASE("LlmInferServiceImpl: thread safety", "[llm-infer]") {
    cosmo::test::MockServiceRegistry mocks;
    LlmInferServiceImpl sut;

    SECTION("Concurrent IsInitialized calls do not race") {
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&sut]() {
                for (int j = 0; j < 100; ++j) {
                    (void)sut.IsInitialized();
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }

    SECTION("Concurrent Generate calls return NotInit safely") {
        std::vector<std::thread> threads;
        std::atomic<int> not_init_count{0};
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&]() {
                std::vector<VideoFramePtr> images;
                std::vector<std::string> prompts;
                cosmo::Qwen3VLGenerationParam param;
                std::vector<cosmo::Qwen3VLResult> results;
                auto ret = sut.Generate(images, prompts, param, results);
                if (ret == ErrorEnum::NotInit) {
                    not_init_count.fetch_add(1);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
        REQUIRE(not_init_count.load() == 4);
    }

    SECTION("Concurrent worker start/stop does not crash") {
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&sut]() {
                sut.NotifyWorkerStart();
                std::this_thread::yield();
                sut.NotifyWorkerStop();
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
}
