#include <cassert>
#include <cstdlib>

#include "media/VideoDemuxer.h"
#include "util/Log.h"
static int open_calls = 0;
extern "C" int __wrap_avformat_open_input(AVFormatContext**, const char*, const AVInputFormat*,
                                          AVDictionary**) {
    ++open_calls;
    return -1;
}
namespace cosmo::log {
size_t LogFormatArg(const char*, const char*, int, int, int, fmt::string_view, fmt::format_args) {
    return 0;
}
}  // namespace cosmo::log
int main() {
    unsetenv("COSMO_AXERA_STRICT_ZERO_COPY");
    cosmo::media::VideoDemuxer demux;
    demux.SetFile("rtsp://127.0.0.1:1/strict-ingress-test");
    assert(demux.OpenStream(false, 1, 0) == cosmo::util::ErrorEnum::DemuxOpenStreamFail);
    assert(open_calls == 0);
}
