#include <cstdint>
#include <type_traits>

#include "service/algorithm/dto/AlgorithmDto.h"

namespace cosmo::test {

static_assert(std::is_same_v<Algorithm::MsgLayoutDetailVersion, service::algorithm::LayoutDetailVersion>);
static_assert(
    std::is_same_v<Algorithm::MsgLayoutDetailSend::ResData, service::algorithm::LayoutDetailResult>);
static_assert(std::is_same_v<Algorithm::MsgLayoutListItem, service::algorithm::LayoutListItem>);
static_assert(std::is_same_v<Algorithm::MsgLayoutListSend::ResData, service::algorithm::LayoutListResult>);
static_assert(std::is_same_v<Algorithm::MsgAtomicAction, service::algorithm::AtomicAction>);
static_assert(
    std::is_same_v<Algorithm::MsgAtomicActionListSend::ResData, service::algorithm::AtomicActionListResult>);
static_assert(
    std::is_same_v<decltype(service::algorithm::LayoutDetailVersion::algorithmUpdateTime), uint64_t>);
static_assert(std::is_base_of_v<MsgSendHead, Algorithm::MsgLayoutDetailSend>);
static_assert(std::is_base_of_v<MsgSendHead, Algorithm::MsgLayoutListSend>);
static_assert(std::is_base_of_v<MsgSendHead, Algorithm::MsgAtomicActionListSend>);

}  // namespace cosmo::test
