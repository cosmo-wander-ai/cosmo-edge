/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    The material in this file is confidential and contains trade secrets
 *    of Sophgo Technologies Inc. This is proprietary information owned by
 *    Sophgo Technologies Inc. No part of this work may be disclosed,
 *    reproduced, copied, transmitted, or used in any way for any purpose,
 *    without the express written permission of Sophgo Technologies Inc.
 *
 *****************************************************************************/

/****************************************************************************
 * This file is the old inference and not suggested to be used in the future
 ****************************************************************************/

#ifndef BMRUNTIME_LEGACY_H_
#define BMRUNTIME_LEGACY_H_

#include "bmdef.h"

#if defined (__cplusplus)
extern "C" {
#endif

/**
 * @name    create_bmruntime
 * @brief   To create the bmruntime
 * @ingroup bmruntime
 *
 * This API creates the bmruntime. It returns a void* pointer which is the pointer
 * of bmruntime. This API default uses the 0-th device.
 *
 * @param [in/out] pbm_handle    BM runtime context. It is input if it had been created before.
 *                               If it is NULL, it will be created as the output.
 *                               This context is for using BMDNN and BMCV.
 * @param [in]     chip_type     Chipname, such as BM1682, BM1684.
 *
 * @retval void* the pointer of bmruntime
 */
#define create_bmruntime(pbm_handle, chip_type, ...) create_bmrt_helper(pbm_handle, chip_type, (0, ##__VA_ARGS__))

/**
 * @name    create_bmrt_helper
 * @brief   To create the bmruntime with choosing device id.
 * @ingroup bmruntime
 *
 * This API creates the bmruntime. It returns a void* pointer which is the pointer
 * of bmruntime. This API can set device id.
 *
 * @param [in/out] pbm_handle    BM runtime context. It is input if it had been created before.
 *                               If it is NULL, it will be created as the output.
 *                               This context is for using BMDNN and BMCV.
 * @param [in]     chip_type     Chipname, such as BM1682, BM1684.
 * @param [in]     devid         ID of device.
 *
 * @retval void* the pointer of bmruntime
 */
void* create_bmrt_helper(void *pbm_handle, const char *chip_type, int devid);

/**
 * @name    destroy_bmruntime
 * @brief   To destroy the bmruntime pointer
 * @ingroup bmruntime
 *
 * This API destroy the bmruntime.
 *
 * @param [in]     p_bmrt        Bmruntime that had been created
 */
void destroy_bmruntime(void* p_bmrt);

/**
 * @name    create_bmruntime_per_device
 * @brief   To create the bmruntime with choosing device id.
 * @ingroup bmruntime
 *
 * This API creates the bmruntime. It returns a void* pointer which is the pointer
 * of bmruntime. This API can set device id.
 *
 * @param [in/out] pbm_handle    BM runtime context. It is input if it had been created before.
 *                               If it is NULL, it will be created as the output.
 *                               This context is for using BMDNN and BMCV.
 * @param [in]     chip_type     Chipname, such as BM1682, BM1684.
 * @param [in]     devid         ID of device.
 *
 * @retval void* the pointer of bmruntime
 */
#define create_bmruntime_per_device(pbm_handle, chip_type, devid) create_bmrt_helper(pbm_handle, chip_type, devid)

/**
 * @name    bmrt_load_context
 * @brief   To load the context which is created by BM compiler
 * @ingroup bmruntime
 *
 * This API is to load context created by BM compiler.
 * After loading context, we can run the inference of neuron network.
 * A context can have one or more neuron network.
 * It can be called again to load other context, then the new context
 * will be appended. And bmruntime will contains more neuron networks.
 * Context directory is a folder.
 *
 * @param   [in]   p_bmrt        Bmruntime that had been created
 * @ctx_dir [in]   ctx_dir       Context directory. Must be a holder.
 *
 * @retval true    Load context sucess.
 * @retval false   Load context failed.
 */
bool bmrt_load_context(void* p_bmrt, const char *ctx_dir);

/**
 * @name    bmrt_can_batch_size_change
 * @brief   To judge if the neuron network is dynamically compiling
 *          and can change batch size
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval ture    The neuron network after compiling can change batch size.
 * @retval false   The neuron network after compiling can not change batch size.
 */
bool bmrt_can_batch_size_change(void* p_bmrt, int net_idx, const char *net_name);

/**
 * @name    bmrt_can_height_and_width_change
 * @brief   To judge if the neuron network is dynamically compiling
 *          and can change input height and width
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval ture    The neuron network after compiling can change input height and width.
 * @retval false   The neuron network after compiling can not change input height and width.
 */
bool bmrt_can_height_and_width_change(void* p_bmrt, int net_idx, const char *net_name);

/**
 * @name    bmrt_get_input_tensor
 * @brief   To get the input information of the neuron network
 * @ingroup bmruntime
 *
 * This API can get the input number and input name of a neuron network.
 * This API can be used after loading context or bmodel.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to disable it.
 * @param [out]    input_num      The input number of the neuron network.
 * @param [out]    input_names    The input names of the neruon network. When we use this API, we can declare
 *                                (const char** inputs_ = NULL), then use &inputs_ as this parameter.
 *                                Bmruntime will create space for inputs_, and we need to free(inputs_)
 *                                if we do not use it.
 */
bool bmrt_get_input_tensor(void* p_bmrt, int net_idx, const char *net_name, int *input_num, const char ***input_names);

/**
 * @name    bmrt_get_output_tensor
 * @brief   To get the output information of the neuron network
 * @ingroup bmruntime
 *
 * This API can get the output number and output name of a neuron network.
 * This API can only be used after loading context or bmodel.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [out]    output_num     The output number of the neuron network.
 * @param [out]    output_names   The output names of the neruon network. When we use this API, we can declare
 *                                (const char** outputs_ = NULL), then use &outputs_ as this parameter.
 *                                Bmruntime will create space for outputs_, and we need to free(outputs_)
 *                                if we do not use it.
 */
bool bmrt_get_output_tensor(void* p_bmrt, int net_idx, const char *net_name, int *output_num, const char ***output_names);

/**
 * @name    bmrt_get_input_blob_max_nhw
 * @brief   To get the max shape of the tensor.
 * @ingroup bmruntime
 *
 * This API must indicate the name of the input tensor and neuron network.
 * Max shape is the max value we can set.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [out]    max_n          The max batch number of the tensor.
 * @param [out]    max_c          The max channels of the tensor.
 * @param [out]    max_h          The max height of the tensor.
 * @param [out]    max_w          The max width of the tensor.
 *
 */

void bmrt_get_input_blob_max_nhw(void* p_bmrt, const char *tensor_name,
                                 int net_idx, const char *net_name,
                                 int * max_n, int * max_c, int * max_h, int * max_w);

/**
 * @name    bmrt_get_input_blob_max_shape
 * @brief   To get the max shape of the tensor.
 * @ingroup bmruntime
 *
 * This API must indicate the name of the input tensor and neuron network.
 * Max shape is the max value we can set.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [out]    shape          The max shape of the tensor.
 * @return         the dims of the tensor
 */

int bmrt_get_input_blob_max_shape(void* p_bmrt, const char *tensor_name,
                                 int net_idx, const char *net_name, int* shape);
/**
 * @name    bmrt_get_output_blob_max_nhw
 * @brief   To get the max shape of the tensor.
 * @ingroup bmruntime
 *
 * This API must indicate the name of the output tensor and neuron network.
 * Max shape is the max value we can set.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [out]    max_n          The max batch number of the tensor.
 * @param [out]    max_c          The max channels of the tensor.
 * @param [out]    max_h          The max height of the tensor.
 * @param [out]    max_w          The max width of the tensor.
 *
 */
void bmrt_get_output_blob_max_nhw(void* p_bmrt, const char *tensor_name,
                                  int net_idx, const char *net_name,
                                  int * max_n, int * max_c, int * max_h, int * max_w);
int bmrt_get_output_blob_max_shape(void* p_bmrt, const char *tensor_name,
                                  int net_idx, const char *net_name, int * shape);

/**
 * @name    bmrt_get_input_data_type
 * @brief   To get the data type of the input tensor
 * @ingroup bmruntime
 *
 * The API is to get the data type of the tensor. Data type include float32, float16, int8, uint8, int16, uint16.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval 0: float32, 1: float16, 2: int8, 3: uint8, 4: int16, 5: uint16.
 */
int bmrt_get_input_data_type(void* p_bmrt, const char *tensor_name, int net_idx, const char *net_name);

/**
 * @name    bmrt_get_output_data_type
 * @brief   To get the data type of the output tensor
 * @ingroup bmruntime
 *
 * The API is to get the data type of the tensor. Data type include float32, float16, int8, uint8, int16, uint16.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval 0: float32, 1: float16, 2: int8, 3: uint8, 4: int16, 5: uint16.
 */
int bmrt_get_output_data_type(void* p_bmrt, const char *tensor_name, int net_idx, const char *net_name);

/**
 * @name    bmrt_get_input_gmem_stmode
 * @brief   To get the data store mode of the input tensor
 * @ingroup bmruntime
 *
 * The API is to get the data store mode of the tensor. Data store mode includes 1N mode, 2N mode, 4N mode.
 * 1N mode is the normal store mode we know.
 * TODO store mode introduction
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval 0: 1N, 1: 2N, 2: 4N
 */
int bmrt_get_input_gmem_stmode(void* p_bmrt, const char *tensor_name, int net_idx, const char *net_name);

/**
 * @name    bmrt_get_output_gmem_stmode
 * @brief   To get the data store mode of the output tensor
 * @ingroup bmruntime
 *
 * The API is to get the data store mode of the tensor. Data store mode includes 1N mode, 2N mode, 4N mode.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     tensor_name    The name of the tensor.
 * @param [in]     net_idx        Neuron network index of the context. This parameter is enable
 *                                if net_name == NULL, otherwise it is disable.
 * @param [in]     net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 *
 * @retval 0: 1N, 1: 2N, 2: 4N
 */
int bmrt_get_output_gmem_stmode(void* p_bmrt, const char *tensor_name, int net_idx, const char *net_name);

/**
 * @name    bmrt_launch
 * @brief   To launch the inference of the neuron network
 * @ingroup bmruntime
 *
 * This API only support the neuron nework that is static-compiled.
 * After calling this API, inference on TPU is launched. And the CPU
 * program will not be blocked.
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The pointer of the input devce memory. It should be bm_device_mem_t*.
 *                               input_tensors[i] must be created by bmrt_malloc.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [out]   output_tensors The pointer of the output devce memory. It should be bm_device_mem_t*.
 *                               output_tensors[i] must be created by bmrt_malloc.
 * @param [in]    output_num     The output number of the neuron network.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch(void* p_bmrt, int net_idx, const char *net_name,
                 const void* input_tensors, int input_num,
                 const void* output_tensors, int output_num, bm_shape_t * output_shapes);

/**
 * @name    bmrt_launch_nhw
 * @brief   To launch the inference of the neuron network with setting input n, h, w
 * @ingroup bmruntime
 *
 * This API supports the neuron nework that is static-compiled or dynamic-compiled
 * After calling this API, inference on TPU is launched. And the CPU
 * program will not be blocked.
 * Note: This API only support one input
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The pointer of the input devce memory. It should be bm_device_mem_t*.
 *                               input_tensors[i] must be created by bmrt_malloc.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [out]   output_tensors The pointer of the output devce memory. It should be bm_device_mem_t*.
 *                               output_tensors[i] must be created by bmrt_malloc.
 * @param [in]    output_num     The output number of the neuron network.
 * @param [in]    n              The batch number of the neuron network.
 * @param [in]    h              The input height of the neuron network.
 * @param [in]    w              The input width of the neuron network.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch_nhw(void* p_bmrt, int net_idx, const char *net_name,
                     const void* input_tensors, int input_num,
                     const void* output_tensors, int output_num,
                     int n, int h, int w, bm_shape_t *output_shapes);

/**
 * @name    bmrt_launch_shape
 * @brief   To launch the inference of the neuron network with setting input shape
 * @ingroup bmruntime
 *
 * This API supports the neuron nework that is static-compiled or dynamic-compiled
 * After calling this API, inference on TPU is launched. And the CPU
 * program will not be blocked.
 * This API support multiple inputs.
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The pointer of the input devce memory. It should be bm_device_mem_t*.
 *                               input_tensors[i] must be created by bmrt_malloc.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [in]    input_dim      The dimension of each input.
 * @param [in]    input_shape    The shape of each input. There are sum(input_dim) elements.
 *                               For example, 3 inputs, their shapes are [4,5,6], [1,3,8,8], [5,5], then
 *                               input_dim={3,4,2}, input_shape={4,5,6,1,3,8,8,5,5}
 * @param [out]   output_tensors The pointer of the output devce memory. It should be bm_device_mem_t*.
 *                               output_tensors[i] must be created by bmrt_malloc.
 * @param [in]    output_num     The output number of the neuron network.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch_shape(void* p_bmrt, int net_idx, const char *net_name,
                       const void* input_tensors, int input_num,
                       const int* input_dim, const int* input_shape,
                       const void* output_tensors, int output_num, bm_shape_t * output_shapes);

/**
 * @name    bmrt_launch_nhw_stmode
 * @brief   To launch the inference of the neuron network with setting input n/h/w and store mode
 * @ingroup bmruntime
 *
 * This API supports the neuron nework that is static-compiled or dynamic-compiled
 * After calling this API, inference on TPU is launched. And the CPU
 * program will not be blocked.
 * Note: This API only support one input of the neuron network.
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The pointer of the input devce memory. It should be bm_device_mem_t*.
 *                               input_tensors[i] must be created by bmrt_malloc.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [out]   output_tensors The pointer of the output devce memory. It should be bm_device_mem_t*.
 *                               output_tensors[i] must be created by bmrt_malloc.
 * @param [in]    output_num     The output number of the neuron network.
 * @param [in]    n              The batch number of the neuron network.
 * @param [in]    h              The input height of the neuron network.
 * @param [in]    w              The input width of the neuron network.
 * @param [in]    in_stmode      User define the store mode of the input.
 * @param [in]    out_stmode     User define the store mode of the output.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch_nhw_stmode(void* p_bmrt, int net_idx, const char *net_name,
                            const void* input_tensors, int input_num,
                            const void* output_tensors, int output_num,
                            int n, int h, int w,
                            int* in_stmode, int* out_stmode, bm_shape_t * output_shapes);

bool bmrt_launch_nhw_stmode_multi_input(void* p_bmrt, int net_idx, const char *net_name,
                                        const void* input_tensors, int input_num,
                                        const void* output_tensors, int output_num,
                                        int n, int* h, int* w,
                                        int* in_stmode, int* out_stmode, bm_shape_t * output_shapes);

/**
 * @name    bmrt_launch_shape_stmode
 * @brief   To launch the inference of the neuron network with setting input shape and store mode
 * @ingroup bmruntime
 *
 * This API supports the neuron nework that is static-compiled or dynamic-compiled
 * After calling this API, inference on TPU is launched. And the CPU
 * program will not be blocked.
 * This API support multiple inputs of the neuron network.
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The pointer of the input devce memory. It should be bm_device_mem_t*.
 *                               input_tensors[i] must be created by bmrt_malloc.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [in]    input_dim      The dimension of each input.
 * @param [in]    input_shape    The shape of each input. If the dimension of the i-th input is x, input_shape[i]
 *                               has x elements. For example, 4 dimension will has 4 elements, input_shape[i][0]
 *                               is N, input_shape[i][1] is C, input_shape[i][2] is H, input_shape[i][3] is W.
 * @param [out]   output_tensors The pointer of the output devce memory. It should be bm_device_mem_t*.
 *                               output_tensors[i] must be created by bmrt_malloc.
 * @param [in]    output_num     The output number of the neuron network.
 * @param [in]    in_stmode      User define the store mode of the input.
 * @param [in]    out_stmode     User define the store mode of the output.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch_shape_stmode(void* p_bmrt, int net_idx, const char *net_name,
                              const void* input_tensors, int input_num,
                              const int* input_dim, const int* input_shape,
                              const void* output_tensors, int output_num,
                              int* in_stmode, int* out_stmode, bm_shape_t * output_shapes);

/**
 * @name    bmrt_launch_cpu_data
 * @brief   To launch the inference of the neuron network with cpu data
 * @ingroup bmruntime
 *
 * This API supports the neuron nework that is static-compiled or dynamic-compiled
 * After calling this API, inference on TPU is launched. And the CPU
 * program will be blocked.
 *
 * @param [in]    p_bmrt         Bmruntime that had been created
 * @param [in]    net_idx        Neuron network index of the context. This parameter is enable
 *                               if net_name == NULL, otherwise it is disable.
 * @param [in]    net_name       The name of the neuron network. It can be NULL if we want to use net_idx.
 * @param [in]    input_tensors  The cpu pointer of the input.
 * @param [in]    input_num      The input number of the neuron network.
 * @param [out]   output_tensors The cpu pointer of the output.
 * @param [in]    output_num     The output number of the neuron network.
 * @param [in]    in_shape       The input shape of the neuron network. Support multiple inputs.
 *                               shape format (net0_n, net0_c, net0_h, net0_w, net1_n, net1_c, net1_h, net1_w)
 * @param [out]   out_shape      The output shape of the neuron network. Support multiple outputs.
 *                               Bmruntime will set out_shape.
 *
 * @retval true    Launch success.
 * @retval false   Launch failed.
 */
bool bmrt_launch_cpu_data(void* p_bmrt, int net_idx, const char *net_name,
                     void* input_tensors, int input_num,
                     void* output_tensors, int output_num,
                     const int* in_shape, bm_shape_t* out_shapes);

/**
 * @name    bmrt_malloc_neuron_device
 * @brief   To allocate device memory by neuron
 * @ingroup bmruntime
 *
 * The size of the device memory by this API is (n * c * h * w * sizeof(float))
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     pmem           Device memory. If use declare device memory as (bm_device_mem_t mem),
 *                                use it as parameter (&mem)
 * @param [in]     size           The number of byte
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_malloc_neuron_device(void* p_bmrt, bm_device_mem_t *pmem, int n, int c, int h, int w);

/**
 * @name    bmrt_malloc_device_byte
 * @brief   To allocate device memory by byte size
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     pmem           Device memory. If use declare device memory as (bm_device_mem_t mem),
 *                                use it as parameter (&mem)
 * @param [in]     size           The number of byte
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_malloc_device_byte(void* p_bmrt, bm_device_mem_t *pmem, unsigned int size);

/**
 * @name    bmrt_free_device
 * @brief   To free the device memory
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [in]     mem            Device memory.
 */
void bmrt_free_device(void* p_bmrt, bm_device_mem_t mem);

/**
 * @name    bmrt_memcpy_s2d
 * @brief   To copy data from system to device
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API is equal to
 * the device memory capacity that user had allocated.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination device memory.
 * @param [in]     src            Source system memory.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_s2d(void* p_bmrt, bm_device_mem_t dst, void* src);

/**
 * @name    bmrt_memcpy_d2s
 * @brief   To copy data from device to system
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API is equal to
 * the device memory capacity that user had allocated.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination system memory.
 * @param [in]     src            Source device memory.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_d2s(void* p_bmrt, void *dst, bm_device_mem_t src);

/**
 * @name    bmrt_memcpy_s2d_withsize
 * @brief   To copy data from system to device with setting size
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination device memory.
 * @param [in]     src            Source system memory.
 * @param [in]     size           The byte size.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_s2d_withsize(void* p_bmrt, bm_device_mem_t dst, void* src, unsigned int size);

/**
 * @name    bmrt_memcpy_d2s_withsize
 * @brief   To copy data from device to system with setting size
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination system memory.
 * @param [in]     src            Source device memory.
 * @param [in]     size           The byte size.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_d2s_withsize(void* p_bmrt, void *dst, bm_device_mem_t src, unsigned int size);

/**
 * @name    bmrt_memcpy_s2d_withsize_offset
 * @brief   To copy data from system to device with setting size and device memory offset
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user. And device memory offset can be also set by user. Data from device memory (address + offset)
 * is transfered.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination device memory.
 * @param [in]     src            Source system memory.
 * @param [in]     size           The byte size.
 * @param [in]     dev_offset     Offset of device memory. Unit is byte.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_s2d_withsize_offset(void* p_bmrt, bm_device_mem_t dst, void* src, unsigned int size, unsigned int dev_offset);

/**
 * @name    bmrt_memcpy_d2s_withsize_offset
 * @brief   To copy data from device to system with setting size and device memory offset
 * @ingroup bmruntime
 *
 * The system memory is allocated by user. The data size transfered by this API can be set
 * by user. And device memory offset can be also set by user. Data from device memory (address + offset)
 * is transfered.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 * @param [out]    dst            destination system memory.
 * @param [in]     src            Source device memory.
 * @param [in]     size           The byte size.
 * @param [in]     dev_offset     Offset of device memory. Unit is byte.
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_memcpy_d2s_withsize_offset(void* p_bmrt, void *dst, bm_device_mem_t src, unsigned int size, unsigned int dev_offset);

/**
 * @name    bmrt_thread_sync
 * @brief   To synchronize cpu program to device
 * @ingroup bmruntime
 *
 * This API is called when user program want to wait for the finishing of device.
 *
 * @param [in]     p_bmrt         Bmruntime that had been created
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_thread_sync(void* p_bmrt);

/**
 * @name    bmrt_dev_getcount
 * @brief   To get the device number
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt         Bmruntime that had been created; NULL is also ok
 * @param [out]    dev_count      The number of device
 *
 * @retval    0      Success
 * @retval    other  fail
 */
int bmrt_dev_getcount(void* p_bmrt, int* dev_count);

/**
 * @name    bmrt_get_last_api_process_time_us
 * @brief   To get last api process time by us
 * @ingroup bmruntime
 *
 * @param [in]     p_bmrt        Bmruntime that had been created
 * @param [out]    time_us       Time in us
 *
 */
void bmrt_get_last_api_process_time_us(void* p_bmrt, unsigned long* time_us);

/**
 * @name    get_bmodel_api_info_c
 * @brief   To get the api info setting input tensors
 * @ingroup bmruntime
 *
 * This API only supports the neuron nework that is static-compiled.
 * After calling this API, api info will be setted and return,
 * and then you can call `bm_send_api` to start TPU inference.
 * When you no longer need the memory, call bmrt_free_api_info to avoid memory leaks.
 *
 * @param [in]    p_bmrt            Bmruntime that had been created
 * @param [in]    net_name          The name of the neuron network
 * @param [in]    input_tensors     Array of input tensor, defined like bm_tensor_t input_tensors[input_num],
 *                                  User should initialize each input tensor.
 * @param [in]    input_num         Input number
 * @param [in]    output_tensors    Array of output tensor, defined like bm_tensor_t output_tensors[output_num].
 *                                  User can set device_mem or stmode of output tensors. If user_mem is true, this interface
 *                                  will use device mem of output_tensors to store output data, and not alloc device mem;
 *                                  Or it will alloc device mem to store output. If user_stmode is true, it will use stmode in
 *                                  each output tensor; Or stmode will be BM_STORE_1N as default.
 * @param [in]    output_num        Output number
 * @param [in]    user_mem          whether device_mem of output tensors are set
 * @param [in]    user_stmode       whether stmode of output tensors are set
 * @param [in]    core_ids          select which cores to uese
 */
api_info_c *get_bmodel_api_info_c(void *p_bmrt, const char *net_name,
                                  const bm_tensor_t *input_tensors, int input_num,
                                  bm_tensor_t *output_tensors, int output_num,
                                  bool user_mem, bool user_stmode, uint32_t *core_ids);

/**
 * @name    bmrt_free_api_info
 * @brief   To release memory allocated by the get_bmodel_api_info_c function's return value.
 * @ingroup bmruntime
 *
 * This function is used to release the memory block returned by the get_bmodel_api_info_c function.
 * After calling get_bmodel_api_info_c to retrieve model API information, make sure to call this function
 * when you no longer need the information to avoid memory leaks.
 *
 * @param [in]    api_info            return value of get_bmodel_api_info_c
 *
 */
void bmrt_free_api_info(api_info_c *api_info);

#if defined (__cplusplus)
}
#endif

#endif
