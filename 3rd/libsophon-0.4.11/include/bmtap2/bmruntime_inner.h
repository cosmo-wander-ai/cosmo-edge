/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    define class bmrt_arch_info to get all architecture infos used locally,
 *    and will not export to users
 *
 *****************************************************************************/

#ifndef LIBBMRUNTIME_INNER_H_
#define LIBBMRUNTIME_INNER_H_

#include <bmruntime.h>
#include <bmruntime_bmnet.h>
#include <stdio.h>

#define RET_IF(_cond, fmt, ...)            \
  do {                                     \
    if ((_cond)) {                         \
      BMRT_LOG(WRONG, fmt, ##__VA_ARGS__); \
      return;                              \
    }                                      \
  } while (0)

#define RET_ERR(fmt, ...)                \
  do {                                   \
    BMRT_LOG(WRONG, fmt, ##__VA_ARGS__); \
    return BM_ERR_FAILURE;               \
  } while (0)

#define RET_ERR_IF(_cond, fmt, ...)        \
  do {                                     \
    if ((_cond)) {                         \
      BMRT_LOG(WRONG, fmt, ##__VA_ARGS__); \
      return BM_ERR_FAILURE;               \
    }                                      \
  } while (0)

#define BM_ASSERT(_cond)                       \
  do {                                         \
    if (!(_cond)) {                            \
      BMRT_LOG(FATAL, "BM_ASSERT: %s", #_cond); \
    }                                          \
  } while (0)

typedef struct bmnet_context {
  bmruntime::Bmruntime *p_bmrt;
  bm_handle_t bm_handle;
  uint32_t current_index;  // For multi static shapes
  int input_num;
  int output_num;
  bm_tensor_t *input_tensors;
  bm_tensor_t *output_tensors;
  bmnet_model_info_t model_info;
} bmnet_context_t;

#endif
