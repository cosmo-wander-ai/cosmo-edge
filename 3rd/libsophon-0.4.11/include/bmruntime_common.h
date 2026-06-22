/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    define common structures used locally, and will not export to users
 *
 *****************************************************************************/
#ifndef BMRUNTIME_COMMON_H
#define BMRUNTIME_COMMON_H

#ifdef __linux__
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <string.h>
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "bmruntime_legacy.h"

#ifdef DEBUG
#define BMRT_DEBUG(fmt, ...)                                                      \
  do {                                                                            \
    printf("[BMRT][%s:%d] DEBUG : " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#else
#define BMRT_DEBUG(fmt, ...)
#endif


constexpr bool strings_equal(char const* a, char const* b)
{
  return *a == *b && (*a == '\0' || strings_equal(a + 1, b + 1));
}

typedef enum {
    DUMP    =-2, // dump data
    DEBUG   =-1,
    INFO    = 0,
    WARNING = 1,
    WRONG   = 2,
    FATAL   = 3,
} BMRT_LogLevel;

extern BMRT_LogLevel BMRT_LOG_LEVEL_THRESHOLD;

#ifdef __cplusplus
extern "C" {
#endif
BMRT_LogLevel bmrt_get_current_log_level();
void bmrt_set_current_log_level(BMRT_LogLevel level);
#ifdef __cplusplus
}
#endif

#ifdef __linux__
template<int level, typename ... ArgTypes>
typename std::enable_if<level<FATAL , void>::type __bmrt_log(const char*fmt, ArgTypes ...args){
    if (level >= BMRT_LOG_LEVEL_THRESHOLD){
        printf(fmt, args...);
    }
}

template<int level, typename ... ArgTypes>
typename std::enable_if<level==FATAL , void>::type __bmrt_log(const char*fmt, ArgTypes ...args){
    if (level >= BMRT_LOG_LEVEL_THRESHOLD){
        printf(fmt, args...);
    }
    throw std::runtime_error("BMRuntime internal error.");
}

#define BMRT_LOG(severity, fmt, ...) \
   __bmrt_log<severity>("[BMRT][%s:%d] %s:" fmt "\n", __func__, __LINE__, #severity, ##__VA_ARGS__)

#define BMRT_LOG_RUN(severity, code)          \
  do                                          \
  {                                           \
    if (severity >= BMRT_LOG_LEVEL_THRESHOLD) \
    {                                         \
      code                                    \
    }                                         \
  } while (0)

#else
#include <stdarg.h>
void bmrt_log_default_callback(int level, const char* fmt, va_list args);
void BMRT_LOG(int level, const char* fmt, ...);
#define BMRT_LOG_RUN(code)
#endif

using std::cout;
using std::endl;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef enum {
  /* keep empty ? in this way, convert int to bm_api_id_t will keep order */
} bm_api_id_t;

#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#ifdef __linux__
#define ALIGN(x, a) __ALIGN_MASK(x, (__typeof__(x))(a)-1)
#else
#define ALIGN(x, a) __ALIGN_MASK(x, (decltype(x))(a)-1)
#endif

#define BMRT_ASSERT_INFO(_cond, fmt, ...)                            \
  do {                                                               \
    if (!(_cond)) {                                                  \
      BMRT_LOG(FATAL, "BMRT_ASSERT: %s" fmt, #_cond, ##__VA_ARGS__); \
    }                                                                \
  } while (0)


#define BMRT_ASSERT(_cond) BMRT_ASSERT_INFO(_cond, "")
#define CHECK_status(status)                                         \
  do{                                                                \
    BMRT_ASSERT_INFO(status == BM_SUCCESS, "launch failed, status:%d", status);\
  }while(0)
static inline int ceiling_func(int numerator, int denominator)
{
  return (numerator + denominator - 1) / denominator;
}

typedef enum {
  INPUT_NEURON_TENSOR = 0,
  INTERMEDIATE_NEURON_TENSOR = 1,
  OUTPUT_NEURON_TENSOR = 2,
  CMD_BUF_TENSOR = 3,
  // CMD_NUM_TENSOR = 4
} NEURON_DEVICE_MEM_TYPE_T;

typedef enum device_mem_type {
  NEURON = 0,
  COEFF = 1,
  /*
  #ifdef INT8_COEFF_FUNC
      COEFF_INT8 = 2,
      COEFF_INT8SCALE = 3,
      LOCAL = 4
  #else
      LOCAL = 2
  #endif
  */
} DEVICE_MEM_TYPE_T;

enum ST_MODE_CHANGE {
  ST_NO_CHANGE = 0,
  ST_4N_TO_1N = 1,
  ST_1N_TO_4N = 2,
  ST_2N_TO_1N = 3,
  ST_1N_TO_2N = 4,
};

typedef enum {
  ENGINE_BD = 0,
  ENGINE_GDMA = 1,
  ENGINE_CDMA = 2,
  ENGINE_HDMA = 3,
  ENGINE_HAU = 4,
  ENGINE_SDMA = 5,
  ENGINE_END
} ENGINE_ID;

typedef enum {
  GDMA_DIR_S2L = 0,
  GDMA_DIR_L2S = 1,
  GDMA_DIR_S2S = 2,
  GDMA_DIR_L2L = 3,
  GDMA_DIR_END
} GDMA_DIRECTION;

typedef struct {
  DEVICE_MEM_TYPE_T device_mem_type;
  NEURON_DEVICE_MEM_TYPE_T neuron_device_mem_type;
  int n;
  int c;
  int h;
  int w;
  int coeff_count;
  int store_mode;  // 0: 1N, 1: 2N, 2: 4N
  unsigned long long address;
  unsigned long size;
  bm_data_type_t data_type;
} DEVICE_MEM_INFO_T;

static inline void shape_to_nchw(int* shape, int dims, int* n, int* c, int* h, int* w)
{
  *n = dims > 0 ? shape[0] : 1;
  *c = dims > 1 ? shape[1] : 1;
  *h = dims > 2 ? shape[2] : 1;
  *w = 1;
  for (int i = 3; i < dims; i++) {
    *w *= shape[i];
  }
}
static inline void shape_to_nblock(int* shape, int dims, int* block_num, int* block_size)
{
  if (block_num)
    *block_num = dims > 0 ? shape[0] : 1;
  if (block_size) {
    *block_size = 1;
    for (int i = 1; i < dims; i++) {
      *block_size *= shape[i];
    }
  }
}

static inline int get_element_num(int* shape, int dims, int n_align_num)
{
  int elt_num = dims > 0 ? ALIGN(shape[0], n_align_num) : 1;
  for (int i = 1; i < dims; i++) {
    elt_num *= shape[i];
  }
  return elt_num;
}

static inline int get_stmode_flag(int stmode, int user_stmode, bool is_input)
{
  if (!is_input) {
    int tmp = stmode;
    stmode = user_stmode;
    user_stmode = tmp;
  }

  int stmode_flag = 0;
  if (user_stmode == BM_STORE_1N && stmode == BM_STORE_4N)
    stmode_flag = ST_1N_TO_4N;
  else if (user_stmode == BM_STORE_1N && stmode == BM_STORE_2N)
    stmode_flag = ST_1N_TO_2N;
  else if (user_stmode == BM_STORE_2N && stmode == BM_STORE_1N)
    stmode_flag = ST_2N_TO_1N;
  else if (user_stmode == BM_STORE_4N && stmode == BM_STORE_1N)
    stmode_flag = ST_4N_TO_1N;
  else BMRT_ASSERT(0);

  return stmode_flag;
}

inline u32 get_mem_index(const std::vector<u64> &ctx_borders, u64 ctx_start, u64 addr)
{
  u32 i = 0;
  for (; i < ctx_borders.size(); ++i)
  {
    if (addr < ctx_start + ctx_borders[i])
      break;
  }
  return i;
}

template<typename T>
std::string vector_to_string(const std::vector<T>& list, const std::string& prefix="[", const std::string& suffix="]", const std::string& sep=", "){
  std::string result = prefix;
  for(size_t i=0; i<list.size(); i++){
    result += std::to_string(list[i]);
    if(i != list.size()-1) {
      result += sep;
    }
  }
  result += suffix;
  return result;
}

static inline const char* dtype_to_string(bm_data_type_t t){
  #define DTYPE_CASE(T) case BM_##T: return #T
  switch(t){
    DTYPE_CASE(FLOAT32);
    DTYPE_CASE(FLOAT16);
    DTYPE_CASE(INT8);
    DTYPE_CASE(UINT8);
    DTYPE_CASE(INT16);
    DTYPE_CASE(UINT16);
    DTYPE_CASE(INT32);
    DTYPE_CASE(UINT32);
    DTYPE_CASE(BFLOAT16);
    DTYPE_CASE(INT4);
    DTYPE_CASE(UINT4);
  }
  return "";
  #undef DTYPE_CASE
}

#if defined(__cplusplus)
extern "C" {
#endif

// declare api mode interface of bmlib
extern bm_status_t bm_send_api(bm_handle_t handle, bm_api_id_t api_id, u8* api, u32 size);

extern bm_status_t bm_sync_api(bm_handle_t handle);

extern int bm_get_devid(bm_handle_t handle);


#ifndef __linux__
#define RTLD_LAZY	0x00001	/* Lazy function call binding.  */
#define RTLD_NOW	0x00002	/* Immediate function call binding.  */
#define RTLD_GLOBAL	0x00100
#define RTLD_LOCAL	0
#endif

void* bmrt_load_lib(const char* libname, int flags);
void* bmrt_find_sym(void* libhandle, const char* symname);
void bmrt_unload_lib(void* libhandle);
const char* bmrt_lib_error();

#if defined(__cplusplus)
}
#endif

const char* _bmrt_version();
const char* _libsophon_version();

#endif
