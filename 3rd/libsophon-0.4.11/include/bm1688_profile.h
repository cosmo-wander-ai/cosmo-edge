#ifndef BM1688_PROFILE_H
#define BM1688_PROFILE_H
#include "bmruntime_profile.h"

using namespace bmruntime;
namespace  bm1688_profile {

#pragma pack(1)
typedef struct {
  // 0x00-0x0F
  unsigned int inst_start_time;
  unsigned int inst_end_time;
  unsigned int inst_id : 20;
  unsigned int reserved0 : 12;
  unsigned int axi_d0_aw_bytes;

  // 0x10-0x1F
  unsigned int axi_d0_wr_bytes;
  unsigned int axi_d0_ar_bytes;
  unsigned int axi_d1_aw_bytes;
  unsigned int axi_d1_wr_bytes;

  // 0x20-0x2F
  unsigned int axi_d1_ar_bytes;
  unsigned int gif_fmem_aw_bytes;
  unsigned int gif_fmem_wr_bytes;
  unsigned int gif_fmem_ar_bytes;

  // 0x30-0x3F
  unsigned int gif_l2sram_aw_bytes;
  unsigned int gif_l2sram_wr_bytes;
  unsigned int gif_l2sram_ar_bytes;
  unsigned int reserved1;

  // 0x40-0x4F
  unsigned int axi_d0_wr_valid_bytes;
  unsigned int axi_d0_rd_valid_bytes;
  unsigned int axi_d1_wr_valid_bytes;
  unsigned int axi_d1_rd_valid_bytes;

  // 0x50-0x5F
  unsigned int gif_fmem_wr_valid_bytes;
  unsigned int gif_fmem_rd_valid_bytes;
  unsigned int gif_l2sram_wr_valid_bytes;
  unsigned int gif_l2sram_rd_valid_bytes;

  // 0x60-0x6F
  unsigned int axi_d0_wr_stall_bytes;
  unsigned int axi_d0_rd_stall_bytes;
  unsigned int axi_d1_wr_stall_bytes;
  unsigned int axi_d1_rd_stall_bytes;

  // 0x70-0x7F
  unsigned int gif_fmem_wr_stall_bytes;
  unsigned int gif_fmem_rd_stall_bytes;
  unsigned int gif_l2sram_wr_stall_bytes;
  unsigned int gif_l2sram_rd_stall_bytes;

  // 0x80-0x8F
  unsigned int axi_d0_aw_end;
  unsigned int axi_d0_aw_st;
  unsigned int axi_d0_ar_end;
  unsigned int axi_d0_ar_st;

  // 0x90-0x9F
  unsigned int axi_d0_wr_end;
  unsigned int axi_d0_wr_st;
  unsigned int axi_d0_rd_end;
  unsigned int axi_d0_rd_st;

  // 0xA0-0xAF
  unsigned int axi_d1_aw_end;
  unsigned int axi_d1_aw_st;
  unsigned int axi_d1_ar_end;
  unsigned int axi_d1_ar_st;

  // 0xB0-0xBF
  unsigned int axi_d1_wr_end;
  unsigned int axi_d1_wr_st;
  unsigned int axi_d1_rd_end;
  unsigned int axi_d1_rd_st;

  // 0xC0-0xCF
  unsigned int reserved2;
  unsigned int reserved3;
  unsigned int gif_fmem_ar_end;
  unsigned int gif_fmem_ar_st;

  // 0xD0-0xDF
  unsigned int gif_fmem_wr_end;
  unsigned int gif_fmem_wr_st;
  unsigned int gif_fmem_rd_end;
  unsigned int gif_fmem_rd_st;

  // 0xE0-0xEF
  unsigned int reserved4;
  unsigned int reserved5;
  unsigned int gif_l2sram_ar_end;
  unsigned int gif_l2sram_ar_st;

  // 0xF0-0xFF
  unsigned int gif_l2sram_wr_end;
  unsigned int gif_l2sram_wr_st;
  unsigned int gif_l2sram_rd_end;
  unsigned int gif_l2sram_rd_st;
}GDMA_PROFILE_FORMAT;

typedef struct {
    unsigned int inst_start_time;
    unsigned int inst_end_time;
    unsigned long long inst_id : 16;
    unsigned long long computation_load : 48;
    unsigned int num_read;
    unsigned int num_read_stall;
    unsigned int num_write;
    unsigned int reserved;
} TPU_PROFILE_FORMAT;
#pragma pack()

typedef struct {
    buffer_pair tiu;
    buffer_pair gdma;
    buffer_pair mcu;
} profile_core_buffer_t;

using namespace bmruntime;
class BMProfileDevice:public BMProfileDeviceBase {

    // BMProfileDeviceBase interface
public:
    BMProfileDevice(BMProfile* profile):bmruntime::BMProfileDeviceBase(profile) { }
    bool init();
    bool begin(net_ctx_t* net_ctx);
    bool end(net_ctx_t* net_ctx);
    void deinit();
    bool enabled();

private:
    std::vector<profile_core_buffer_t> buffers;
};

}
#endif // BM1686_PROFILE_H
