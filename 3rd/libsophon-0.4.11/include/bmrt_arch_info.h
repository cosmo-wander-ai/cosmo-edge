/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    define class bmrt_arch_info to get all architecture infos used locally,
 *    and will not export to users
 *
 *****************************************************************************/

#ifndef BMRT_ARCH_INFO_H_
#define BMRT_ARCH_INFO_H_

#include "bmruntime_common.h"

namespace bmruntime {

typedef enum bmtpu_arch {
  BM1682,
  BM1684,
  BM1880,
  BM1684X,
  BM1688,
  BM1690,
  MARS3,
  SG2380,
  UNKOWN_ARCH
} bmtpu_arch_t;

class bmrt_arch_info {
  public:
    bmrt_arch_info(const string& arch_name);
    ~bmrt_arch_info();

    static bmtpu_arch_t get_bmtpu_arch() { return sta_bmtpu_ptr->target_bmtpu_arch; }
    static const string& get_bmtpu_name() { return sta_bmtpu_ptr->m_arch_name; }
    static bool is_soc_mode() { return sta_bmtpu_ptr->m_soc_mode; }
    static void set_current_arch_info(bmrt_arch_info *arch_ptr) { if(arch_ptr) sta_bmtpu_ptr = arch_ptr; }


    static int get_npu_num();
    static int get_eu_num(bm_data_type_t dtype);
    static int get_lmem_size();
    static int get_lmem_bank_size();
    static int get_lmem_banks();

    static u64 get_gmem_start();
    static u64 get_gmem_start_soc();
    static u64 get_gmem_offset_soc();
    static u64 get_gmem_cmd_start_offset();
    static u64 get_ctx_start_addr();
    static u64 get_bdc_engine_cmd_aligned_bit();
    static u64 get_gdma_engine_cmd_aligned_bit();
    static u32 get_bdc_cmd_num();
    static u32 get_gdma_cmd_num();
    static u64 get_soc_base_distance();

    static u64 addr_mask();

  private:
    static bmrt_arch_info* sta_bmtpu_ptr;
    bmtpu_arch_t target_bmtpu_arch;
    string m_arch_name;
    bool m_soc_mode;
};

#define EU_NUM                  (bmrt_arch_info::get_eu_num())
#define NPU_NUM                 (bmrt_arch_info::get_npu_num())
#define LOCAL_BANK_SIZE         (bmrt_arch_info::get_lmem_bank_size())
#define LOCAL_MEM_SIZE          (bmrt_arch_info::get_lmem_size())
#define GLOBAL_MEM_START_ADDR   (bmrt_arch_info::get_gmem_start())
#define GLOBAL_MEM_CMD_START_OFFSET (bmrt_arch_info::get_gmem_cmd_start_offset())
#define BDC_ENGINE_CMD_ALIGNED_BIT (bmrt_arch_info::get_bdc_engine_cmd_aligned_bit())
#define GDMA_ENGINE_CMD_ALIGNED_BIT (bmrt_arch_info::get_gdma_engine_cmd_aligned_bit())
#define BD_ENGINE_COMMAND_NUM_aligned  (1 << BDC_ENGINE_CMD_ALIGNED_BIT)/sizeof(u32)
#define GDMA_ENGINE_COMMAND_NUM_aligned  (1 << GDMA_ENGINE_CMD_ALIGNED_BIT)/sizeof(u32)
#define BD_ENGINE_COMMAND_NUM (bmrt_arch_info::get_bdc_cmd_num())
#define GDMA_ENGINE_COMMAND_NUM (bmrt_arch_info::get_gdma_cmd_num())
#define CTX_START_ADDR (bmrt_arch_info::get_ctx_start_addr())

}

#endif
