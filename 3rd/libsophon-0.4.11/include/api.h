#pragma once

#include "bmlib_type.h"

typedef enum {
  BM_API_ID_RESERVED                         = 0,
  BM_API_ID_MEM_SET                          = 1,
  BM_API_ID_MEM_CPY                          = 2,
  BM_API_ID_MEMCPY_BYTE                      = 136,
  BM_API_ID_MEMCPY_WSTRIDE                   = 137,
  BM_API_ID_SET_PROFILE_ENABLE               = 986,
  BM_API_ID_GET_PROFILE_DATA                 = 987,
  BM_API_ID_SET_ENGINE_PROFILE_PARAM         = 988,
  BM_API_TPUKERNEL_MULTICORE                 = 0x0fffffff,
  BM_API_ID_START_CPU                        = 0x80000001,
  BM_API_ID_OPEN_PROCESS                     = 0x80000002,
  BM_API_ID_LOAD_LIBRARY                     = 0x80000003,
  BM_API_ID_EXEC_FUNCTION                    = 0x80000004,
  BM_API_ID_MAP_PHY_ADDR                     = 0x80000005,
  BM_API_ID_CLOSE_PROCESS                    = 0x80000006,
  BM_API_ID_SET_LOG                          = 0x80000007,
  BM_API_ID_GET_LOG                          = 0x80000008,
  BM_API_ID_SET_TIME                         = 0x80000009,
  BM_API_ID_UNLOAD_LIBRARY                   = 0x8000000b,
  BM_API_ID_A53LITE_LOAD_LIB                 = 0x90000001,
  BM_API_ID_A53LITE_GET_FUNC                 = 0x90000002,
  BM_API_ID_A53LITE_LAUNCH_FUNC              = 0x90000003,
  BM_API_ID_TPUSCALER_LAUNCH_FUNC_MULT_CORES  = 0x90000013,
  BM_API_ID_A53LITE_UNLOAD_LIB               = 0x90000004,
  BM_API_ID_TPUSCALER_LOAD_LIB               = BM_API_ID_A53LITE_LOAD_LIB,
  BM_API_ID_TPUSCALER_GET_FUNC               = BM_API_ID_A53LITE_GET_FUNC,
  BM_API_ID_TPUSCALER_LAUNCH_FUNC            = BM_API_ID_A53LITE_LAUNCH_FUNC,
  BM_API_ID_TPUSCALER_UNLOAD_LIB             = BM_API_ID_A53LITE_UNLOAD_LIB,
  BM_API_ID_CPU_MAX,
} sglib_api_id_t;
#pragma pack(push, 1)
typedef struct bm_api_memset {
  u64 global_offset;
  unsigned int height;
  unsigned int width;
  int mode;
  int val;
} bm_api_memset_t;

typedef struct bm_api_memcpy_byte {
  u64 src_global_offset;
  u64 dst_global_offset;
  u64 size;
}bm_api_memcpy_byte_t;

typedef struct bm_api_memcpy_wstride {
  u64 src_global_offset;
  u64 dst_global_offset;
  int src_wstride;
  int dst_wstride;
  int count;
  int format_bytes;
}bm_api_memcpy_wstride_t;

typedef struct bm_api_memcpy {
  u64 src_global_offset;
  u64 dst_global_offset;
  int input_n;
  int src_nstride;
  int dst_nstride;
  int count;
}bm_api_memcpy_t;

typedef struct {
    int core_idx;
    int core_num;
    int core_msg_id;
    int name_len;
    int api_id;
    int api_size;
    unsigned char api_data[0];
#ifndef WIN32
} __attribute__((packed)) sg_api_core_info_t;
#else
} sg_api_core_info_t;
#endif
#pragma pack(pop)
