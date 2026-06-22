#include "nn/utils/tracker/abstract_filter.h"

#include <mutex>

namespace cosmo::nn {

AbstractFilter::AbstractFilter(DeviceType type) : device_type(type) {}

AbstractFilter::~AbstractFilter() {}

int AbstractFilter::GetWidth() const {
    return width;
}

void AbstractFilter::SetWidth(int v) {
    width = v;
}

int AbstractFilter::GetHeight() const {
    return height;
}

void AbstractFilter::SetHeight(int v) {
    height = v;
}

void AbstractFilter::SetPosProb(float v) {
    pos_prob = v;
}

float AbstractFilter::GetPosProb() const {
    return pos_prob;
}

void AbstractFilter::SetNegProb(float v) {
    neg_prob = v;
}

float AbstractFilter::GetNegProb() const {
    return neg_prob;
}

void AbstractFilter::SetSensiValue(int v) {
    sensi_value = v;
}

int AbstractFilter::GetSensiValue() const {
    return sensi_value;
}

void AbstractFilter::SetRoi(int v) {
    roi = v;
}

int AbstractFilter::GetRoi() const {
    return roi;
}

int AbstractFilter::GetElementCount() const {
    return GetWidth() * GetHeight();
}

Status AbstractFilter::Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "AbstractFilter::Update is not implemented");
}

std::shared_ptr<FilterManager>& FilterManager::Instance() {
    static std::once_flag filter_once;
    static std::shared_ptr<FilterManager> g_global_filter_manager;
    std::call_once(filter_once, []() { g_global_filter_manager = std::make_shared<FilterManager>(); });
    return g_global_filter_manager;
}

FilterManager::FilterManager() {}

FilterManager::~FilterManager() {}

std::shared_ptr<AbstractFilter> FilterManager::CreateFilter(DeviceType type) {
    auto iter = filter_creater_map.find(type);
    if (iter != filter_creater_map.end()) {
        return iter->second->CreateFilter(type);
    }
    return nullptr;
}

int FilterManager::RegisterAbstractFilterCreater(DeviceType device_type,
                                                 std::shared_ptr<AbstractFilterCreater> creater) {
    auto iter = filter_creater_map.find(device_type);
    if (iter != filter_creater_map.end()) {
        LOGI("INFO: device_type(%d) cannot be registered twice\n", device_type);
        return 1;
    }

    if (!creater) {
        LOGE("Error: AbstractFilterCreater is nullptr device_type(%d)\n", device_type);
        return 1;
    }

    filter_creater_map[device_type] = creater;
    return 0;
}

}  // namespace cosmo::nn