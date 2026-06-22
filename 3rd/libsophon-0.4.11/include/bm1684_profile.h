#ifndef BM1684_PROFILE_H
#define BM1684_PROFILE_H
#include "bmruntime_profile.h"
using namespace bmruntime;

namespace  bm1684_profile {
typedef struct tpu_profile_format {
    unsigned int inst_start_time;
    unsigned int inst_end_time;
    unsigned long long inst_id : 16;
    unsigned long long computation_load : 48;
    unsigned int num_read;
    unsigned int num_read_stall;
    unsigned int num_write;
    unsigned int reserved;
}TPU_PROFILE_FORMAT;

typedef struct gdma_profile_format {
    unsigned int inst_start_time;
    unsigned int inst_end_time;
    unsigned int inst_id : 16;
    unsigned int reserved: 16;
    unsigned int d0_aw_bytes;
    unsigned int d0_wr_bytes;
    unsigned int d0_ar_bytes;
    unsigned int d1_aw_bytes;
    unsigned int d1_wr_bytes;
    unsigned int d1_ar_bytes;
    unsigned int gif_aw_bytes;
    unsigned int gif_wr_bytes;
    unsigned int gif_ar_bytes;
    unsigned int d0_wr_valid_cyc;
    unsigned int d0_rd_valid_cyc;
    unsigned int d1_wr_valid_cyc;
    unsigned int d1_rd_valid_cyc;
    unsigned int gif_wr_valid_cyc;
    unsigned int gif_rd_valid_cyc;
    unsigned int d0_wr_stall_cyc;
    unsigned int d0_rd_stall_cyc;
    unsigned int d1_wr_stall_cyc;
    unsigned int d1_rd_stall_cyc;
    unsigned int gif_wr_stall_cyc;
    unsigned int gif_rd_stall_cyc;
    unsigned int d0_aw_end;
    unsigned int d0_aw_st;
    unsigned int d0_ar_end;
    unsigned int d0_ar_st;
    unsigned int d0_wr_end;
    unsigned int d0_wr_st;
    unsigned int d0_rd_end;
    unsigned int d0_rd_st;
    unsigned int d1_aw_end;
    unsigned int d1_aw_st;
    unsigned int d1_ar_end;
    unsigned int d1_ar_st;
    unsigned int d1_wr_end;
    unsigned int d1_wr_st;
    unsigned int d1_rd_end;
    unsigned int d1_rd_st;
    unsigned int gif_aw_reserved1;
    unsigned int gif_aw_reserved2;
    unsigned int gif_ar_end;
    unsigned int gif_ar_st;
    unsigned int gif_wr_end;
    unsigned int gif_wr_st;
    unsigned int gif_rd_end;
    unsigned int gif_rd_st;
}GDMA_PROFILE_FORMAT;
unsigned int fix_gdma_monitor_data(unsigned char* aligned_data, unsigned int len);

class BMProfileDevice:public bmruntime::BMProfileDeviceBase {

    // BMProfileDeviceBase interface
public:
    BMProfileDevice(BMProfile* profile):BMProfileDeviceBase(profile) { }
    bool init();
    bool begin(net_ctx_t* net_ctx);
    bool end(net_ctx_t* net_ctx);
    void deinit();
    bool enabled();

private:
    buffer_pair tpu_buffer;
    buffer_pair gdma_buffer;
    buffer_pair dyn_buffer;
    bm_perf_monitor tpu_perf_monitor;
    bm_perf_monitor gdma_perf_monitor;
    bm_device_mem_t aligned_mem;

    // BMProfileDeviceBase interface
};

}
#endif // BM1684_PROFILE_H
