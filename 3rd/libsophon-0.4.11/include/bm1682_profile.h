#ifndef BM1682_PROFILE_H
#define BM1682_PROFILE_H
#include <vector>
#include <string>

namespace bm1682_profile {

std::vector<unsigned char>& get_log();
void bm_log_profile_callback(int api, int result, int duration, const char * log_buffer);
}

#endif // BM1682_PROFILE_H
