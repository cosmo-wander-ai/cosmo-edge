#ifndef BMDNN_FUNC_H_
#define BMDNN_FUNC_H_

#include "bmruntime_common.h"
#include "bmruntime_cpp.h"

namespace bmruntime {

struct tpu_tensor_info_t {
  uint16_t dtype;
  /// storage mode of input/output tensors which are setted by user
  uint16_t user_stmode;
  /// storage mode of input/output tensors which are fixed when compiling
  uint16_t compiled_stmode;
  uint32_t tensor_byte_size;
  int32_t n;
  int32_t c;
  int32_t h;
  int32_t w;
  /// value of padding h for conv(only used for BM1684 conv 3ic)
  uint32_t padding_h;
  /// global addr that is malloced by user
  uint64_t user_global_addr;
  /// global addr of input/output tensors which are fixed when compiling
  uint64_t compiled_global_addr;
};
struct tpu_cmd_info_t {
  /// number of bdc command
  int32_t bdc_cmd_num;
  /// number of gdma command
  int32_t gdma_cmd_num;
  /// number of cdma command
  int32_t cdma_cmd_num;
  /// byte size of bdc command
  uint64_t bdc_cmd_byte_size;
  /// byte size of gdma command
  uint64_t gdma_cmd_byte_size;
  /// byte size of cdma command
  uint64_t cdma_cmd_byte_size;
};
struct tpu_single_core_cmd_t {
  std::vector<tpu_cmd_info_t> cmd_info;
  /// global addr of bdc command
  uint64_t bdc_cmd_addr;
  /// global addr of gdma command
  uint64_t gdma_cmd_addr;
  /// global addr of cdma command
  uint64_t cdma_cmd_addr;
  /// global addr of hau command
  uint64_t hau_cmd_addr;
  //// global addr of sdma command
  uint64_t sdma_cmd_addr;
};

typedef struct tpu_kernel_allreduce_1684x {
  u64     i_global_addr[8];
  u64     i_global_addr_1[8];
  u64     o_global_addr[8];
  u32     count;
  int     dtype;
  int     reduce_method;
  int     group[8];
  int     rank;
  int     chip_num;
  int     group_size;
} tpu_kernel_allreduce_1684x_t;

typedef struct tpu_kernel_global_move_1684x {
  u64     src_global_addr;
  u64     dst_global_addr;
  int     num_dims;
  int     shape[4];
  int     src_stride[4];
  int     dst_stride[4];
  int     type_size;
} tpu_kernel_global_move_1684x_t;

typedef struct {
  std::vector<tpu_tensor_info_t> input_info;
  std::vector<tpu_tensor_info_t> output_info;
  std::vector<tpu_single_core_cmd_t> core_commands;
  std::vector<int32_t> core_list;
  /// kernel func id(used for dynamic loading)
  std::vector<tpu_kernel_function_t> kernel_func_ids;
  /// coeff start addr
  uint64_t coeff_start_addr = -1;
  /// neuron start addr
  std::vector<uint64_t> neuron_start_addr;
  int32_t do_allreduce = 0;
  tpu_kernel_allreduce_1684x_t allreduce_param;
  int32_t addr_mode;
} tpu_net_info_t;

class bmdnn_func {
  public:
    bmdnn_func() {};
    virtual ~bmdnn_func() {};

    virtual bm_status_t
    _bmdnn_multi_fullnet_(bm_handle_t handle,
                          const tpu_net_info_t &net_info) = 0;
    virtual void fill_api_info(const tpu_net_info_t &net_info,
                               api_info_t &api_info) = 0;
};

class bmdnn_func_1682 : public bmdnn_func {
  public:

    bmdnn_func_1682() {
        BM_API_ID_MULTI_FULLNET      = 134;
        BM_API_ID_DYNAMIC_FULLNET    = 135;
        BM_API_ID_MULTI_FULLNET_PROFILE = 137;
        MAX_API_MSG_SIZE             = 1019 * sizeof(u32);  //ref to 1682
    };
    void set_bmdnn_func_profile(bool enable) {
        b_enable_profile = enable;
    }
    virtual bm_status_t _bmdnn_multi_fullnet_(
            bm_handle_t handle,
            const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_v2_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length,  //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        unsigned long long apd_ctx_start,
        unsigned long long apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        unsigned int using_arm_buffer_size
    );
  private:
    bool b_enable_profile = false;
    u32 BM_API_ID_MULTI_FULLNET_PROFILE;
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_1684 : public bmdnn_func {
  public:

    bmdnn_func_1684() {
        BM_API_ID_MULTI_FULLNET       = 110;
        BM_API_ID_DYNAMIC_FULLNET     = 111;
        BM_API_ID_SET_PROFILE_ENABLE  = 986;
        BM_API_ID_GET_PROFILE_DATA    = 987;
        MAX_API_MSG_SIZE              = 1022 * sizeof(u32); // ref to 1684
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);
    bm_status_t _bmdnn_dynamic_fullnet_v2_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length,  //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const unsigned long long *input_middle_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        const unsigned long long *output_middle_addrs,
        unsigned long long apd_ctx_start,
        std::vector<unsigned long long> apd_ctx_mem_borders,
        std::vector<unsigned long long> apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        bool need_middle_buff_flag,
        unsigned int* output_need_middle_buff_flag,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        unsigned int using_arm_buffer_size
    );
    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, bool enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );

  private:
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 BM_API_ID_SET_PROFILE_ENABLE;
    u32 BM_API_ID_GET_PROFILE_DATA;
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_1880 : public bmdnn_func {
  public:

    bmdnn_func_1880() {
        ;
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_v2_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length,
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const unsigned long long *input_middle_addrs,
        const int * const * input_shapes,
        const int * input_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        const unsigned long long *output_middle_addrs,
        unsigned long long apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        bool need_middle_buff_flag,
        unsigned int* output_need_middle_buff_flag,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        unsigned int using_arm_buffer_size
    );

  private:
};

class bmdnn_func_1684x : public bmdnn_func {
  public:

    bmdnn_func_1684x() {
        BM_API_ID_MULTI_FULLNET       = 0x0ffffffb;
        BM_API_ID_DYNAMIC_FULLNET     = 0x0ffffffc;
        BM_API_ID_SET_PROFILE_ENABLE  = 986;
        BM_API_ID_GET_PROFILE_DATA    = 987;
        MAX_API_MSG_SIZE              = 1016 * sizeof(u32);
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        tpu_kernel_function_t func_id,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length, //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        unsigned long long apd_ctx_start,
        std::vector<unsigned long long> apd_ctx_mem_borders,
        std::vector<unsigned long long> apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        unsigned long long apd_io_start,
        unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        tpu_kernel_allreduce_1684x_t *p_allreduce_param);

    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, tpu_kernel_function_t func_id, bool enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         tpu_kernel_function_t func_id,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );
  private:
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 BM_API_ID_SET_PROFILE_ENABLE;
    u32 BM_API_ID_GET_PROFILE_DATA;
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_1688 : public bmdnn_func {
  public:

    bmdnn_func_1688() {
        MAX_API_MSG_SIZE              = 1016 * sizeof(u32);
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        const std::vector<tpu_kernel_function_t> & func_id_list,
        const unsigned long long compiled_ir_global_addr,
        const unsigned int compiled_ir_length, //unit dword
        const unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        const unsigned int output_num,
        const unsigned long long *output_addrs,
        const unsigned long long apd_ctx_start,
        const std::vector<unsigned long long> apd_ctx_mem_borders,
        const std::vector<unsigned long long> apd_ctx_mem_offset,
        const unsigned long long apd_coeff_mem_offset,
        const unsigned long long apd_io_start,
        const unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        const unsigned long long output_shape_global_addr,
        const std::vector<int32_t> &core_list);

    bm_status_t _bmdnn_set_engine_profile_param_(bm_handle_t handle, int core, tpu_kernel_function_t func_id, int engine_type, unsigned long long addr, unsigned long long size);
    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, int core, tpu_kernel_function_t func_id, unsigned int enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         int core,
                                         tpu_kernel_function_t func_id,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );
  private:
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_2260 : public bmdnn_func {
  public:

    bmdnn_func_2260() {
        BM_API_ID_MULTI_FULLNET       = 0x0ffffffb;
        BM_API_ID_DYNAMIC_FULLNET     = 0x0ffffffc;
        BM_API_ID_SET_PROFILE_ENABLE  = 986;
        BM_API_ID_GET_PROFILE_DATA    = 987;
        MAX_API_MSG_SIZE              = 1016 * sizeof(u32);
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length, //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        unsigned long long apd_ctx_start,
        std::vector<unsigned long long> apd_ctx_mem_borders,
        std::vector<unsigned long long> apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        unsigned long long apd_io_start,
        unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        const std::vector<int32_t> &core_list);

    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, unsigned int enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );
  private:
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 BM_API_ID_SET_PROFILE_ENABLE;
    u32 BM_API_ID_GET_PROFILE_DATA;
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_mars3 : public bmdnn_func {
  public:

    bmdnn_func_mars3() {
        BM_API_ID_MULTI_FULLNET       = 0x0ffffffb;
        BM_API_ID_DYNAMIC_FULLNET     = 0x0ffffffc;
        BM_API_ID_SET_PROFILE_ENABLE  = 986;
        BM_API_ID_GET_PROFILE_DATA    = 987;
        MAX_API_MSG_SIZE              = 1016 * sizeof(u32);
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        unsigned long long compiled_ir_global_addr,
        unsigned int compiled_ir_length, //unit dword
        unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        unsigned int output_num,
        const unsigned long long *output_addrs,
        unsigned long long apd_ctx_start,
        std::vector<unsigned long long> apd_ctx_mem_borders,
        std::vector<unsigned long long> apd_ctx_mem_offset,
        unsigned long long apd_coeff_mem_offset,
        unsigned long long apd_io_start,
        unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        unsigned long long output_shape_global_addr,
        const std::vector<int32_t> &core_list);

    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, unsigned int enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );
  private:
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 BM_API_ID_SET_PROFILE_ENABLE;
    u32 BM_API_ID_GET_PROFILE_DATA;
    u32 MAX_API_MSG_SIZE;
};

class bmdnn_func_2380 : public bmdnn_func {
  public:

    bmdnn_func_2380() {
        BM_API_ID_MULTI_FULLNET       = 0x0ffffffb;
        BM_API_ID_DYNAMIC_FULLNET     = 0x0ffffffc;
        BM_API_ID_SET_PROFILE_ENABLE  = 986;
        BM_API_ID_GET_PROFILE_DATA    = 987;
        MAX_API_MSG_SIZE              = 1016 * sizeof(u32);
    };
    virtual bm_status_t _bmdnn_multi_fullnet_(
        bm_handle_t handle,
        const tpu_net_info_t &net_info);
    virtual void fill_api_info(
        const tpu_net_info_t &net_info,
        api_info_t &api_info);

    bm_status_t _bmdnn_dynamic_fullnet_(
        bm_handle_t handle,
        const unsigned long long compiled_ir_global_addr,
        const unsigned int compiled_ir_length, //unit dword
        const unsigned int input_num,
        const unsigned long long *input_addrs,
        const int * const * input_shapes,
        const int * input_elem_nums,
        const int * input_dtype_and_dims,
        const unsigned int output_num,
        const unsigned long long *output_addrs,
        const unsigned long long apd_ctx_start,
        const std::vector<unsigned long long> apd_ctx_mem_borders,
        const std::vector<unsigned long long> apd_ctx_mem_offset,
        const unsigned long long apd_coeff_mem_offset,
        unsigned long long apd_io_start,
        const unsigned long long apd_io_mem_offset,
        bool get_output_shape,
        const unsigned long long output_shape_global_addr,
        const std::vector<int32_t> &core_list);

    bm_status_t _bmdnn_set_engine_profile_param_(bm_handle_t handle, int core, tpu_kernel_function_t func_id, int engine_type, unsigned long long addr, unsigned long long size);
    bm_status_t _bmdnn_set_profile_enable_(bm_handle_t handle, int core, tpu_kernel_function_t func_id, unsigned int enable);
    bm_status_t _bmdnn_get_profile_data_(bm_handle_t handle,
                                         int core,
                                         tpu_kernel_function_t func_id,
                                         unsigned long long output_global_addr,
                                         unsigned int output_max_size,
                                         unsigned int offset,
                                         unsigned int data_category //0: profile time records, 1: extra data
                                         );
  private:
    u32 BM_API_ID_MULTI_FULLNET;
    u32 BM_API_ID_DYNAMIC_FULLNET;
    u32 BM_API_ID_SET_PROFILE_ENABLE;
    u32 BM_API_ID_GET_PROFILE_DATA;
    u32 MAX_API_MSG_SIZE;
};

}

#endif
