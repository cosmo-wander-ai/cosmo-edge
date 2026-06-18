#pragma once

#include <map>
#include <memory>

#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {

class AbstractFilter {
public:
    explicit AbstractFilter(DeviceType);

    virtual ~AbstractFilter();

    virtual Status Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average);

    virtual Status SetDeviceId(int id = 0) = 0;

    virtual Status Clear() = 0;

    void SetWidth(int w);
    int GetWidth() const;

    void SetHeight(int h);
    int GetHeight() const;

    void SetPosProb(float v);
    float GetPosProb() const;

    void SetNegProb(float v);
    float GetNegProb() const;

    void SetSensiValue(int v);
    int GetSensiValue() const;

    void SetRoi(int v);
    int GetRoi() const;

    int GetElementCount() const;

protected:
    DeviceType device_type;

    int width  = 100;
    int height = 100;

    float pos_prob  = 50;
    float neg_prob  = 13;
    int sensi_value = 128;

    /**
     * 0 - 100
     */
    int roi = 10;
};

class AbstractFilterCreater {
public:
    virtual ~AbstractFilterCreater(){};
    virtual std::shared_ptr<AbstractFilter> CreateFilter(DeviceType type) = 0;
};

class FilterManager {
public:
    static std::shared_ptr<FilterManager>& Instance();

    FilterManager();
    ~FilterManager();

    std::shared_ptr<AbstractFilter> CreateFilter(DeviceType type);

    int RegisterAbstractFilterCreater(DeviceType device_type, std::shared_ptr<AbstractFilterCreater> creater);

private:
    std::map<DeviceType, std::shared_ptr<AbstractFilterCreater>> filter_creater_map;
};

template <typename T>
class FilterRegister {
public:
    explicit FilterRegister(DeviceType device_type) {
        auto& manager = FilterManager::Instance();
        auto creater  = std::make_shared<T>();
        manager->RegisterAbstractFilterCreater(device_type, creater);
    };

    ~FilterRegister(){};
};

#define DECLARE_FILTER_CREATER(device)                                                                       \
    class device##FilterCreater : public AbstractFilterCreater {                                             \
    public:                                                                                                  \
        virtual ~device##FilterCreater(){};                                                                  \
        virtual std::shared_ptr<AbstractFilter> CreateFilter(DeviceType type) {                              \
            return std::make_shared<device##Filter>(type);                                                   \
        };                                                                                                   \
    }

#define REGISTER_FILTER(device, device_type)                                                                 \
    FilterRegister<device##FilterCreater> g_filter_##device(device_type)

}  // namespace cosmo::nn
