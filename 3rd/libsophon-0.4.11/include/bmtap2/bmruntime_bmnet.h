/**
 * @mainpage BMNET Runtime Interface: bmruntime_bmnet.h
 * @file   bmruntime_bmnet.h
 * @brief  BMNET runtime APIs.
 *
 * This file describes BMNET runtime interface APIs.
 */

/****************************************************************************
 * These APIs are old bmtap2 c inference, not suggested to be used in the future.
 * Only support static nets, and only load one net in multi-net bmodel.
 * Suggest to use API in bmruntime_interface.h
 ****************************************************************************/

#ifndef _BMRUNTIME_BMNET_H_
#define _BMRUNTIME_BMNET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bmlib_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t fmt_t;
#define FMT_F32 0
#define FMT_F16 1
#define FMT_I32 2
#define FMT_I16 3
#define FMT_I8 4
#define FMT_I4 5
#define FMT_I2 6
#define FMT_I1 7
#define FMT_U16 8
#define FMT_U8 9
#define FMT_INVALID 10

typedef struct {
  uint32_t dim;
  uint32_t n;
  uint32_t c;
  union {
    uint32_t h;
    uint32_t row;
  };
  union {
    uint32_t w;
    uint32_t col;
  };
} shape_t;

typedef bm_handle_t bmctx_t;
typedef bm_status_t bmerr_t;
typedef struct bm_mem_desc bmmem_device_t;

/**
 * @name    bmnet_input_info_t
 * @brief   Structure to describe input info of a BMNET.
 * @ingroup bmruntime
 */
typedef struct bmnet_input_info_s {
  size_t input_size;     // !< total input size in bytes
  size_t input_num;      // !< input number
  shape_t *shape_array;  // !< array of pointers to the input shapes
  size_t *size_array;    // !< array of pointers to the input size in bytes
  uint32_t *fmt_array;   // !< array of pointers to the input fmt, like FMT_I8/FMT_F32
} bmnet_input_info_t;

/**
 * @name    bmnet_output_info_t
 * @brief   Structure to describe output info of a BMNET.
 * @ingroup bmruntime
 */
typedef struct bmnet_output_info_s {
  size_t output_size;    // !< total output size in bytes
  size_t output_num;     // !< output number
  char **name_array;     // !< array of pointers to the output names
  shape_t *shape_array;  // !< array of pointers to the output shapes
  size_t *size_array;    // !< array of pointers to the output size
  uint32_t *fmt_array;   // !< array of pointers to the output fmt, like FMT_I8/FMT_F32
} bmnet_output_info_t;

/**
 * @name    bmnet_model_info_t
 * @brief   Structure to describe model info of a BMNET.
 *          it contains all inputs and outputs info
 * @ingroup bmruntime
 */
typedef struct bmnet_model_info_s {
  char *net_name;                          // !< net name in bmodel
  uint32_t chip;                           // !< 1682 or 1684
  uint32_t command_num;                    // !< command number in bmodel
  bmnet_input_info_t *input_info_array;    // !< input info for each command
  bmnet_output_info_t *output_info_array;  // !< output info for each command
} bmnet_model_info_t;

struct bmnet_context;
/**
 * @name    bmnet_t
 * @brief   BMNET runtime context handler.
 * @ingroup bmruntime
 */
typedef struct bmnet_context *bmnet_t;

/* --------------------------------------------------------------------------*/
/* BM context interface */

/**
 * @name    bm_init
 * @brief   Initialize a BM168x device and create a handle to BM context
 * @ingroup bmruntime
 *
 * This API initialize a BM device and create a handle to BM context.
 * And run in BM168x
 *
 * @param [in]  index      Device index; set 0 as default
 * @param [out] ctx        The pointer of BM context handle
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bm_init(int index, bmctx_t *ctx);

/**
 * @name    bm_exit
 * @brief   Exit BM context, and release device resource
 * @ingroup bmruntime
 *
 * This API must be called before application exits. It will release all internal resources.
 *
 * @param [in] ctx        The pointer of BM context handle
 *
 */
void bm_exit(bmctx_t ctx);

/**
 * @name    bmmem_device_alloc_raw
 * @brief   To allocate device memory by byte size
 * @ingroup bmruntime
 *
 * @param [in] ctx        The pointer of BM context handle
 * @param [in] size       Byte size
 *
 * @retval bmmem_device_t Device memory handle
 */
bmmem_device_t bmmem_device_alloc_raw(bmctx_t ctx, size_t size);

/**
 * @name    bmmem_device_free
 * @brief   To free the device memory
 * @ingroup bmruntime
 *
 * @param [in] ctx      The pointer of BM context handle
 * @param [in] mem      Device memory handle
 *
 */
void bmmem_device_free(bmctx_t ctx, bmmem_device_t mem);

/**
 * @name    bm_memcpy_s2d
 * @brief   To copy data from system to device
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API is equal to
 * the device memory capacity that user had allocated.
 *
 * @param [in]     ctx      The pointer of BM context handle
 * @param [out]    dst      Destination device memory.
 * @param [in]     src      Source system memory.
 *
 * @retval    BM_SUCCESS    Successfully
 * @retval    other         Fail
 */
extern bmerr_t bm_memcpy_s2d(bmctx_t ctx, bmmem_device_t dst, void *src);

/**
 * @name    bm_memcpy_d2s
 * @brief   To copy data from device to system
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API is equal to
 * the device memory capacity that user had allocated.
 *
 * @param [in]     ctx      The pointer of BM context handle
 * @param [out]    dst      Destination system memory.
 * @param [in]     src      Source device memory.
 *
 * @retval    BM_SUCCESS    Successfully
 * @retval    other         Fail
 */
extern bmerr_t bm_memcpy_d2s(bmctx_t ctx, void *dst, bmmem_device_t src);

/**
 * @name    bm_memcpy_s2d_ex
 * @brief   To copy data from system to device with setting size and device memory offset
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user. And device memory offset can be also set by user. Data from device memory (address +
 * offset) is transfered.
 *
 * @param [in]     ctx      The pointer of BM context handle
 * @param [out]    dst      Destination device memory.
 * @param [in]     src      Source system memory.
 * @param [in]     offset   Offset of device memory. Unit is byte.
 * @param [in]     size     The byte size.
 *
 * @retval    BM_SUCCESS    Successfully
 * @retval    other         Fail
 */
bmerr_t bm_memcpy_s2d_ex(bmctx_t ctx, bmmem_device_t dst, void *src, uint64_t offset, size_t size);

/**
 * @name    bm_memcpy_d2s_ex
 * @brief   To copy data from device to system with setting size and device memory offset
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user. And device memory offset can be also set by user. Data from device memory (address +
 * offset) is transfered.
 *
 * @param [in]     ctx      The pointer of BM context handle
 * @param [out]    dst      Destination system memory.
 * @param [in]     src      Source device memory.
 * @param [in]     offset   Offset of device memory. Unit is byte.
 * @param [in]     size     The byte size.
 *
 * @retval    BM_SUCCESS    Successfully
 * @retval    other         Fail
 */
bmerr_t bm_memcpy_d2s_ex(bmctx_t ctx, void *dst, bmmem_device_t src, uint64_t offset, size_t size);

/* --------------------------------------------------------------------------*/
/* BM net interface */

/**
 * @name    bmnet_register_bmodel
 * @brief   To register a neuron network (bmnet) to BM runtime,
 *          with bmodel file.
 * @ingroup bmruntime
 *
 * This API registers a neuron network (bmnet) to BM runtime, with bmodel
 * file. Compared to bmnet_register, this API use bmodel file to register
 * neuron network directly, instead of generating weight.bin and cmdbuf.bin
 * firstly. Besides, this API can support different input shape, so the user
 * must set input shape later.
 *
 * @param [in]  ctx        BM runtime context.
 * @param [in]  bmodel     bmodel filename.
 * @param [out] net        BMNET context handler.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_register_bmodel(bmctx_t ctx, const char *bmodel, bmnet_t *net);

/**
 * @name    bmnet_register_bmodel_data
 * @brief   To register a neuron network (bmnet) to BM runtime,
 *          with bmodel data in memory.
 * @ingroup bmruntime
 *
 * This API registers a neuron network (bmnet) to BM runtime, with bmodel
 * data. Compared to bmnet_register, this API use bmodel data to register
 * neuron network directly, instead of generating weight.bin and cmdbuf.bin
 * firstly. Besides, this API can support different input shape, so the user
 * must set input shape later.
 *
 * @param [in]  ctx           BM runtime context.
 * @param [in]  bmodel_data   bmodel data in memory.
 * @param [in]  size          bmodel data size in byte.
 * @param [out] net           BMNET context handler.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_register_bmodel_data(bmctx_t ctx, void *bmodel_data, size_t size, bmnet_t *net);

/**
 * @name    bmnet_set_input_shape
 * @brief   To set ipnut shape for a registered bmnet.
 * @ingroup bmruntime
 *
 * This API set input shape for a registered bmnet. The bmodel or caffemodel
 * net may suport different input shape, this API can set the input shape in
 * these shapes. This API only match one input shape.
 *
 * @param [in]  ctx    BM runtime context.
 * @param [in]  shape  input shape.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_set_input_shape(bmnet_t net, shape_t shape);

/**
 * @name    bmnet_set_input_shape2
 * @brief   To set multiple ipnut shapes for a registered bmnet.
 * @ingroup bmruntime
 *
 * This API set multiple input shapes for a registered bmnet. The bmodel or
 * caffemodel net may suport different input shape, this API can set the input
 * shapes in these shapes.
 *
 * @param [in]  ctx    BM runtime context.
 * @param [in]  shape  input shape array.
 * @param [in]  num    number of input shapes.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_set_input_shape2(bmnet_t net, shape_t *shape, int num);

/**
 * @name    bmnet_get_output info
 * @brief   To get output info of current command.
 * @ingroup bmruntime
 *
 * This API get output info of current input shape. The bmodel or caffemodel
 * net may suport different input shape, this API can get output info of the
 * current input shape.
 *
 * @param [in]  ctx          BM runtime context.
 * @param [out] output_info  output information.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_get_output_info(bmnet_t net, bmnet_output_info_t *output_info);

/**
 * @name    bmnet_get_input info
 * @brief   To get input info of current command.
 * @ingroup bmruntime
 *
 * This API get input info. The bmodel or caffemodel
 * net may suport different input shape, this API can get input info of the
 * current input shape.
 *
 * @param [in]  ctx          BM runtime context.
 *
 * @retval input_info  input information.
 * @retval NULL        if get failed
 */
const bmnet_input_info_t *bmnet_get_input_info(bmnet_t net);

/**
 * @name    bmnet_get_input info
 * @brief   To get input info of current ipnut shape.
 * @ingroup bmruntime
 *
 * This API get input info of current input shape. The bmodel or caffemodel
 * net may suport different input shape, this API can get output info of the
 * current input shape.
 *
 * @param [in]  ctx          BM runtime context.
 * @param [out] input_info   input information.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
const bmnet_model_info_t *bmnet_get_model_info(bmnet_t net);

/**
 * @name    bmnet_cleanup
 * @brief   To cleanup a registered bmnet.
 * @ingroup bmruntime
 *
 * This API cleanups a registered bmnet. It frees all resources allocated
 * during register and runtime.
 *
 * @param [in]  net    BMNET context handler.
 *
 * @retval
 */
void bmnet_cleanup(bmnet_t net);

/**
 * @name    bmnet_run
 * @brief   To run a registered bmnet.
 * @ingroup bmruntime
 *
 * This API runs a registered bmnet, by sending the cmdbuf to hardware, and
 * wait for it to finish. It assumes input and weight are already loaded into
 * device memory. If cmdbuf_preload is set, it also assumes cmdbuf is already
 * loaded into device memory, otherwise, cmdbuf will be sent to device memory
 * within this function.
 *
 * @param [in]  ctx    BM runtime context.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_run(bmnet_t net);

/**
 * @name    bmnet_load_input
 * @brief   To load input data for a registered bmnet.
 * @ingroup bmruntime
 *
 * This API loads input data for a registered bmnet, from system memory. A
 * memory copy from system memory to device memory happens during loading.
 *
 * @param [in]  ctx    BM runtime context.
 * @param [in]  input  Pointer to the input buffer.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_load_input(bmnet_t net, void *input);

/**
 * @name    bmnet_store_output
 * @brief   To store output data for a registered bmnet.
 * @ingroup bmruntime
 *
 * This API stores output data for a registered bmnet, to system memory. A
 * memory copy from device memory to system memory happens during storing.
 *
 * @param [in]  ctx    BM runtime context.
 * @param [in]  output Pointer to the output buffer.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_store_output(bmnet_t net, void *output);

/**
 * @name    bmnet_inference
 * @brief   To run inference with a registered bmnet
 * @ingroup bmruntime
 *
 * This API runs inference with a registered bmnet, with both input and output
 * data in system memory. Memory copy between device memory and system memory
 * happens during the call.
 *
 * @param [in]  net    BM net handle.
 * @param [in]  input  Pointer to the input buffer.
 * @param [in]  output Pointer to the output buffer.
 *
 * @retval BM_SUCCESS  Successfully.
 * @retval others      Error code.
 */
bmerr_t bmnet_inference(bmnet_t net, void *input, void *output);

#ifdef __cplusplus
}
#endif

#endif /* _BMRUNTIME_BMNET_H_ */
