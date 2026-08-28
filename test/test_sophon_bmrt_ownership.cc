#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_SOPHON_BACKEND

#include <memory>
#include <type_traits>
#include <utility>

#include "nn/core/shared_resource.h"
#include "nn/device/sophon/sophon_net_node.h"

namespace cosmo::nn {
namespace {

    static_assert(std::is_nothrow_move_constructible_v<OwnedBmrt>);
    static_assert(!std::is_copy_constructible_v<OwnedBmrt>);

    TEST_CASE("Sophon BMRuntime ownership rejects an empty handle", "[nn][sophon][ownership]") {
        SophonNetNode node;
        OwnedBmrt runtime;

        auto status = node.AttachOwnedBmrt(std::move(runtime));
        REQUIRE(static_cast<int>(status) == static_cast<int>(COSMO_NN_ERR_LOAD_MODEL));
    }

    TEST_CASE("Sophon graph handles stay isolated and are recycled without device teardown",
              "[nn][sophon][ownership]") {
        auto first  = std::make_unique<SharedResource>(0);
        auto second = std::make_unique<SharedResource>(0);

        REQUIRE(first->m_handle != nullptr);
        REQUIRE(second->m_handle != nullptr);
        REQUIRE(first->m_handle != second->m_handle);
        CHECK(bm_get_devid(first->m_handle) == 0);
        CHECK(bm_get_devid(second->m_handle) == 0);

        const bm_handle_t released_handle = first->m_handle;
        first.reset();

        // Destroying one graph must not disturb another active graph. The next
        // graph reuses the idle process-lifetime handle instead of requesting
        // and freeing a BM context during task stop/start churn.
        CHECK(bm_get_devid(second->m_handle) == 0);
        auto recycled = std::make_unique<SharedResource>(0);
        CHECK(recycled->m_handle == released_handle);
        CHECK(recycled->m_handle != second->m_handle);
    }

}  // namespace
}  // namespace cosmo::nn

#endif
