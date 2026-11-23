#pragma once
#include <array>
#include <string>

namespace cosmo {
enum class NativePairRole { Face, Person, BenchPerson, BenchHelmet, Other };
struct NativePairMember {
    std::string channel;
    std::string task;
    NativePairRole role;
};
inline bool IsSupportedNativePair(const std::string& channel,
                                  const std::array<NativePairMember, 2>& members) {
    bool face = false, person = false, bench_person = false, bench_helmet = false;
    if (channel.empty())
        return false;
    for (const auto& member : members) {
        if (member.channel != channel)
            return false;
        if (!face && member.role == NativePairRole::Face && member.task == channel + "_60581")
            face = true;
        else if (!person && member.role == NativePairRole::Person && member.task == channel + "_74470")
            person = true;
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        else if (!bench_person && member.role == NativePairRole::BenchPerson &&
                 member.task == channel + "_930927510")
            bench_person = true;
        else if (!bench_helmet && member.role == NativePairRole::BenchHelmet &&
                 member.task == channel + "_930007463")
            bench_helmet = true;
#endif
        else
            return false;
    }
    return (face && person) || (bench_person && bench_helmet);
}
}  // namespace cosmo
