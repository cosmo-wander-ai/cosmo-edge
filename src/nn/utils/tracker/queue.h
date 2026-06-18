#pragma once

#include <vector>

namespace cosmo::nn {

template <typename T>
class Queue {
public:
    Queue(int length = 10) {
        max_length = length;
    }

    void push_back(const T& item) {
        if (data.size() >= max_length) {
            data.erase(data.begin());
        }
        data.push_back(item);
    }

    int size() {
        return data.size();
    }
    int length() {
        return max_length;
    }

    T operator[](const int idx) {
        if (idx >= data.size()) {
            return data.back();
        }
        return data[idx];
    }

    std::vector<T> get_data() {
        return data;
    }

private:
    std::vector<T> data;
    int max_length;
};

}  // namespace cosmo::nn
