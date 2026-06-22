// AlgorithmMapper.h — Service-to-wire DTO mapping functions.
// Eliminates manual field-by-field copy in MessageAlgorithmHandler.

#pragma once

#include "service/algorithm/dto/AlgorithmDto.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service::algorithm {

/// Map a LayoutDetailResult (service DTO) to the wire DTO for API response.
inline void ToWire(const LayoutDetailResult& src, Algorithm::MsgLayoutDetailSend::ResData& dst) {
    dst.algorithmCode        = src.algorithmCode;
    dst.algorithmName        = src.algorithmName;
    dst.algorithmCategory    = src.algorithmCategory;
    dst.algorithmUsage       = src.algorithmUsage;
    dst.supplier             = src.supplier;
    dst.remark               = src.remark;
    dst.confVersionId        = src.confVersionId;
    dst.algorithmMetadata    = src.algorithmMetadata;
    dst.algorithmProcessdata = src.algorithmProcessdata;
    dst.atomicList           = src.atomicList;
    dst.configVersionList.clear();
    dst.configVersionList.reserve(src.configVersionList.size());
    for (const auto& v : src.configVersionList) {
        Algorithm::MsgLayoutDetailVersion mv;
        mv.id                   = v.id;
        mv.name                 = v.name;
        mv.algorithmCode        = v.algorithmCode;
        mv.algorithmMetadata    = v.algorithmMetadata;
        mv.algorithmProcessdata = v.algorithmProcessdata;
        mv.atomicList           = v.atomicList;
        mv.algorithmUpdateTime  = v.algorithmUpdateTime;
        dst.configVersionList.push_back(std::move(mv));
    }
}

/// Map a LayoutListResult (service DTO) to a vector of wire DTO items.
inline void ToWire(const LayoutListResult& src, std::vector<Algorithm::MsgLayoutListItem>& dst) {
    dst.clear();
    dst.reserve(src.list.size());
    for (const auto& item : src.list) {
        Algorithm::MsgLayoutListItem mItem;
        mItem.algorithmCode  = item.algorithmCode;
        mItem.algorithmName  = item.algorithmName;
        mItem.supplier       = item.supplier;
        mItem.algorithmUsage = item.algorithmUsage;
        mItem.description    = item.description;
        dst.push_back(std::move(mItem));
    }
}

/// Map an AtomicActionListResult (service DTO) to a vector of wire DTO items.
inline void ToWire(const AtomicActionListResult& src, std::vector<Algorithm::MsgAtomicAction>& dst) {
    dst.clear();
    dst.reserve(src.list.size());
    for (const auto& item : src.list) {
        Algorithm::MsgAtomicAction mItem;
        mItem.id               = item.id;
        mItem.name             = item.name;
        mItem.actionName       = item.actionName;
        mItem.inputParamConfig = item.inputParamConfig;
        mItem.actionUsage      = item.actionUsage;
        mItem.actionType       = item.actionType;
        dst.push_back(std::move(mItem));
    }
}

}  // namespace cosmo::service::algorithm
