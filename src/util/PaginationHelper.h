#pragma once
#include <cstddef>
#include <vector>

namespace cosmo::util {

class PaginationHelper {
public:
    /**
     * @brief General pagination: supports custom Filter and Mapper.
     * Iterates all valid elements to calculate accurate total.
     */
    template <typename InputIt, typename FilterFunc, typename MapFunc>
    static auto Paginate(InputIt first, InputIt last, int pageNum, int pageSize, size_t& total,
                         FilterFunc&& filter, MapFunc&& mapper) {
        using ResultType = decltype(mapper(*first));
        std::vector<ResultType> result;

        total = 0;
        if (pageNum < 1 || pageSize < 1)
            return result;

        size_t indexStart = static_cast<size_t>(pageNum - 1) * static_cast<size_t>(pageSize);
        size_t indexEnd   = static_cast<size_t>(pageNum) * static_cast<size_t>(pageSize);
        size_t index      = 0;

        for (auto it = first; it != last; ++it) {
            if (!filter(*it)) {
                continue;
            }
            total++;
            index++;
            if (index > indexStart && index <= indexEnd) {
                result.push_back(mapper(*it));
            }
        }

        return result;
    }

    /**
     * @brief Optimized pagination: early break after indexEnd for scenarios without filtering (or total is
     * known).
     */
    template <typename InputIt, typename MapFunc>
    static auto PaginateKnownTotal(InputIt first, InputIt last, int pageNum, int pageSize, size_t totalItems,
                                   MapFunc&& mapper) {
        using ResultType = decltype(mapper(*first));
        std::vector<ResultType> result;

        if (pageNum < 1 || pageSize < 1 || totalItems == 0)
            return result;

        size_t indexStart = static_cast<size_t>(pageNum - 1) * static_cast<size_t>(pageSize);
        size_t indexEnd   = static_cast<size_t>(pageNum) * static_cast<size_t>(pageSize);
        size_t index      = 0;

        for (auto it = first; it != last; ++it) {
            index++;
            if (index > indexStart && index <= indexEnd) {
                result.push_back(mapper(*it));
            } else if (index > indexEnd) {
                break;  // Performance optimization: early break when no filtering needed
            }
        }

        return result;
    }
};

}  // namespace cosmo::util
