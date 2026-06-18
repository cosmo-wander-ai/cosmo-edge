/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    define class bmruntime with all runtime functions used locally, and will
 *    not export to users
 *
 *****************************************************************************/

#ifndef BMRUNTIME_H_
#define BMRUNTIME_H_
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include "bmfunc/bmfunc.h"
//#include "bmcpu.h"
#include "bmruntime_common.h"
#include "bmruntime_profile.h"

#include "bmodel.hpp"
#include "bmlib_runtime.h"
#include <atomic>

using bmodel::CoeffMem;
using bmodel::ModelCtx;
using bmodel::NetParameter;
using bmodel::Net;
using flatbuffers::Offset;
using flatbuffers::Vector;

#ifdef _WIN32
#ifndef DECL_EXPORT
#define DECL_EXPORT _declspec(dllexport)
#endif
#ifndef DECL_IMPORT
#define DECL_IMPORT _declspec(dllimport)
#endif
#else
#define DECL_EXPORT
#define DECL_IMPORT
#endif

#ifndef __linux__
int bmrt_clock_gettime(int dummy, struct timespec* ct);
#endif

namespace bmruntime {

// class defined in this file.
class Bmruntime;
class BmCoeff;
class KernelModule;

struct BmMemory {
  string desc; // description
  bm_device_mem_t device_mem;
  u8 check_code[bmodel::SHA256_LEN]; // sha256
  u64 addr;
  u32 bytes;
  u32 dword_len;
  bm_handle_t bm_handle;
  bool do_check;

  void Init(const string &desc, bm_handle_t handle, const bm_device_mem_t &mem,
            void *buffer, bool do_check = false);
  void Update(const bm_device_mem_t &mem, void *buffer);
  u32 GetBytes() {return bytes;}
  int GetData(void* buffer);
  int Check();
};

struct single_core_command_t {
  vector<int> gdma_id;  // for static
  vector<int> bdc_id;   // for static
  vector<u32> gdma_cmd_byte; // for static
  vector<u32> bdc_cmd_byte; // for static

  BmMemory gdma_mem;    // for static
  BmMemory bdc_mem;     // for static
  BmMemory hau_mem;     // for static
  BmMemory sdma_mem;    // for static
  u64 gdma_offset;      // for static subnet
  u64 bdc_offset;       // for static subnet

  BmMemory ir_mem;      // for dynamic
  u32 ir_offset;        // for dynamic subnet
  u32 ir_len;           // for dynamic subnet
};
typedef struct subnet_tpu_info {
  subnet_tpu_info()
  {
    core_commands.clear();
  }
  int is_dynamic;
  std::vector<single_core_command_t> core_commands;
} SUBNET_TPU_INFO_T;

/* TODO: reuse cpu_layer_param_t */
typedef struct subnet_cpu_info {
  subnet_cpu_info()
  {
    op_type = -1;
    user_param = NULL;
    param_size = 0;
  }
  int op_type;
  void* user_param;
  int param_size;
} SUBNET_CPU_INFO_T;

typedef struct {
    /* for merge subnet, output[i] is selected from {input[output_from[i][0]...input[output_from[i][N]]} */
    vector<vector<int>> output_from;
} SUBNET_MERGE_INFO_T;

typedef struct {
    /* for switch subnet, output[i] is from input[output_from[i]] */
    vector<int> output_from;
    /* for switch subnet, for i-th output, output_branch[i]=0 means the output is to false branch, otherwise to true branch */
    vector<int> output_branch;
    bool valid;
} SUBNET_SWITCH_INFO_T;

#define SUBNET_MODE_TPU 0
#define SUBNET_MODE_CPU 1
#define SUBNET_MODE_MERGE 2
#define SUBNET_MODE_SWITCH 3
typedef struct {
  int subnet_mode; /* 0 : tpu, 1: cpu */
  /* union could not used if include extensible vector */
  SUBNET_TPU_INFO_T tpu_info;
  SUBNET_CPU_INFO_T cpu_info;
  SUBNET_MERGE_INFO_T merge_info;
  SUBNET_SWITCH_INFO_T switch_info;

  /* per subnet i/o tensor */
  vector<string> input_tensor_name_v;
  vector<string> output_tensor_name_v;

  int id;
  vector<int> next_subnet_ids;
} SUBNET_INFO_T;

typedef struct {
  bm_shape_t shape;
  bm_store_mode_t st_mode;
  bm_device_mem_t dev_mem;
  u32 pad_h;
} tensor_attr_t;

typedef enum {
  HOST_MEM_INVALID       = 0,
  HOST_MEM_ALLOC         = 1,   /* Allocated internal, need free */
  HOST_MEM_MMAP          = 2,   /* Mmap from tensor dev_mem,  need unmmap */
  //HOST_MEM_OUTSIDE     = 3,   /* Memory from outside, do nothing ? */
} host_mem_type_t;

typedef struct {
  void*               addr;
  u32                 tensor_cpu_addr;
  u64                 size;
  host_mem_type_t     type;
} host_mem_t;

typedef enum {
  MEM_TYPE_INVALID  = 0,
  MEM_TYPE_TPU      = (1 << 0),
  MEM_TYPE_CPU      = (1 << 1),
} mem_type_t;

typedef enum {
  TENSOR_TYPE_INVALID        = 0,
  TENSOR_TYPE_NET_INPUT      = (1 << 0),
  TENSOR_TYPE_NET_OUTPUT     = (1 << 1),
  TENSOR_TYPE_IMM_IO         = (1 << 2),
} tensor_io_type_t;

/* record host mem in addition to device mem for
 * cpu layer tensor.
 */
typedef struct {
  bm_tensor_t         tensor_info;
  bm_shape_t          max_shape;
  host_mem_t          host_mem;
  int                 mem_type;
  tensor_io_type_t    io_type;
  int                 io_index;   /* index fro net input/output */
  SUBNET_INFO_T*      src_subnet; /* src subnet for imm i/o tensor */
  int                 record_elem_num; /*if 0, do not use it, the real elem num can be compute from shape. if not 0, use it*/
  unsigned int        pad_h; /* pad_h for conv 3ic */
} tensor_ext_t;

typedef struct {
  uint64_t addr;
  uint64_t size;
  uint64_t offset;
  string desc;
  bool operator == (const string &desc) const {
    return this->desc == desc;
  }
} device_mem_info_t;

struct net_stage_t {
  vector<tensor_attr_t> input_v;
  vector<tensor_attr_t> output_v;
  u64 coeff_offset;
  u64 ctx_start;
  vector<u64> ctx_offset;
  vector<u64> ctx_borders;
  std::vector<bm_device_mem_u64_t> neuron_mem;
  std::vector<u64> neuron_size;
  // for dynamic if bmodel combined
  u64 dynamic_coeff_offset;
  u64 dynamic_ctx_start;
  vector<u64> dynamic_ctx_offset;

  std::vector<single_core_command_t> core_commands;

  // io alone
  u64 io_start;
  u64 io_size;
  u64 io_offset;
  bm_device_mem_t io_mem;

  // have multi subnet
  int subnet_num;                  /* subnet num per net */
  vector<SUBNET_INFO_T*> subnet_v; /* subnet per net */

  /* subnet i/o tensor in addtion to net i/o tensor */
  map<string, tensor_ext_t> subnet_tensor_v;
  // save the profile info
  vector<u8> net_profile;
  // save the net stat info
  vector<u8> net_stat;
  // for cpu layer
  u32 cpu_mem_size;
  float* cpu_addr;
};

/* Record post dynamic alloc neuron usage info
   detailed to each stage and core permutations
*/
struct dyn_neuron_stage_t {
  vector<tensor_attr_t> input_v;
  vector<tensor_attr_t> output_v;
  vector<u64> ctx_offset;
  vector<u64> dynamic_ctx_offset;
  std::vector<bm_device_mem_u64_t> neuron_mem;

  map<string, tensor_ext_t> subnet_tensor_v;
  float* cpu_addr;
};

struct neuron_mem_block {
  uint64_t key;
  bool used;
};

struct net_ctx_t {
  string net_name;
  vector<string> input_name_v;
  vector<bm_data_type_t> input_type_v;
  vector<float> input_scale_v;
  vector<int> input_zero_point_v;
  vector<string> output_name_v;
  vector<bm_data_type_t> output_type_v;
  vector<float> output_scale_v;
  vector<int> output_zero_point_v;
  vector<net_stage_t *> stage_v;              // each net has multi stages
  std::unordered_map<uint64_t, dyn_neuron_stage_t *> dyn_neuron_stage_dict;   // {neron_code: dyn_neuron_stage_info}
  vector<neuron_mem_block> mem_block;

  // Bulk neuron memories.
  vector<bm_device_mem_u64_t> neuron_mem;
  vector<u64> neuron_size;
  std::unordered_map<string, uint64_t> mem_info_dict;

  std::mutex neuron_mutex;                    // to avoid neuron mem used by other thread
  bool is_dynamic = 0;
  int core_num = 1;
  int n_can_change = 0;                           // for dynamic
  int h_w_can_change = 0;                         // for dynamic
  vector<bm_device_mem_t> middlebuff_input;   // for dynamic, one net share one middlebuf
  vector<bm_device_mem_t> middlebuff_output;  // for dynamic
  bm_net_info_t net_info;                     // create for users by c interface
  std::shared_ptr<KernelModule> kernel_module_;

  // net with cascade
  int32_t device_id = 0;
  int32_t step_id = 0;
  bool in_cascade = false;
  int32_t addr_mode = 0;
  vector<int> input_from; // input is loaded from which device
  vector<int> input_hidden_v;
  vector<int> input_index_v;
  vector<int> output_hidden_v;
  vector<int> output_index_v;
  int32_t do_allreduce = 0;
  tpu_kernel_allreduce_1684x_t allreduce_param;
};

// net with cascade
struct mem_cascade_t {
  string name;
  int32_t device_id;
  bm_tensor_t tensor;
};

struct net_cascade_t {
  string main_name;                  // net name
  int num_device;                    // num device used
  vector<vector<int>> step_ids;      // each step of nets
  std::vector<mem_cascade_t> hidden_inputs;
  std::vector<mem_cascade_t> hidden_outputs;
  std::vector<int> hidden_inputs_step_ids;
  std::vector<int> hidden_outputs_step_ids;

  vector<string> input_names;
  vector<bm_data_type_t> input_types;
  vector<float> input_scales;
  vector<int> input_zps;
  vector<bm_shape_t> input_shapes;
  vector<size_t> input_bytes;
  vector<bm_device_mem_t> input_mems;
  vector<int> input_loc_devices;

  vector<string> output_names;
  vector<bm_data_type_t> output_types;
  vector<float> output_scales;
  vector<int> output_zps;
  vector<bm_shape_t> output_shapes;
  vector<size_t> output_bytes;
  vector<bm_device_mem_t> output_mems;
  vector<int> output_loc_devices;
  int32_t addr_mode;
  bool is_dynamic;

  bm_net_info_t net_info;
};

class CascadeThread;

class Bmruntime {
 public:
  Bmruntime(bm_handle_t* bm_handle, bool user_initlized, const string& arch_name);
  Bmruntime(const string& arch_name, int devid);
  Bmruntime(bm_handle_t* bm_handles, int num_handles,
            bool using_internal_hiddens, const string& arch_name);

   /* free memory */
  void uninit_cpu_handles();
  void free_device_memory();
  void free_network_contexts_and_cascades();
  void free_coeff_mem();
  void free_bm_handle_and_custom_cpu_handles();
  void destory_without_coeff();
  ~Bmruntime();

  friend class BMProfile;

  void set_debug_mode(int mode);
  void set_bmrt_mmap(bool enable);
  void subnet_time_print(bool enable);
  bool load_context(const string& ctx_dir);
  bool load_bmodel(const string& filepath);
  bool load_bmodel(const void* bmodel_data, size_t size);
  bool load_bmodel_with_mem(const string& filepath, mem_info_t* mem_info);
  bool load_bmodel_with_mem(const void* bmodel_data, size_t size, mem_info_t* mem_info);
  bool load_bmodel_with_decrypt(const string &filepath, const std::string &decrypt_lib);
  bool load_bmodel_with_decrypt(const string &filepath, decrypt_func f);
  void set_device_mem_info(ModelCtx* model_ctx, mem_info_t* mem_info);
  bool update_bmodel_weight_with_decrypt(
    const string& filepath, const string& update_path, const string& net_idx,
    const string& mem_idx, const std::vector<std::string>& weight_idx, decrypt_func f);
  bool empty_bmodel_weight_with_decrypt(
    const string& filepath, const string& net_idx,
    const string& mem_idx, const std::vector<std::string>& weight_idx, decrypt_func f);

  /* C++ style Interface */
  const vector<string>* get_input_tensor(int net_idx) const;
  const vector<string>* get_output_tensor(int net_idx) const;
  const vector<u8>* get_net_profile(int net_idx, int stage_idx);
  void init_output_tensors(net_ctx_t* net_ctx, net_stage_t* stage,
                           bm_tensor_t* output_tensors, bool user_mem, bool user_stmode);


  /* C style Interface */
  bool get_input_tensor(int net_idx, int* input_num, const char*** input_names) const;
  bool get_output_tensor(int net_idx, int* output_num, const char*** output_names) const;

  /* use full shape info */
  bool launch(int net_idx, const int input_num, const bm_device_mem_t* input_mems,
              int* input_shapes, int* input_dims, int* in_stmode, int output_num,
              const bm_device_mem_t* output_mems, int* out_stmode, bm_shape_t * output_shapes = NULL);
  bool launch(int net_idx, const bm_tensor_t *input_tensors, int input_num,
              bm_tensor_t *output_tensors, int output_num, bool user_mem = false,
              bool user_stmode = false);
  bool launch_multi_cores(int net_idx, const bm_tensor_t *input_tensors,
                          int input_num, bm_tensor_t *output_tensors,
                          int output_num, uint64_t thread_idx,
                          const std::vector<int> &core_list,
                          bool user_mem, bool user_stmode, bool using_thread=false);
  bool launch_multi_cores(int net_idx, void* const input_datas[], const bm_shape_t input_shapes[],
              int input_num, void* output_tensors[], bm_shape_t output_shapes[], int output_num,
              uint64_t thread_idx, bool user_mem = false, const std::vector<int>& core_list={}, bool using_thread=false);
  bool launch(const net_cascade_t * net_c, const bm_tensor_t* input_tensors, int input_num,
              bm_tensor_t* output_tensors, int output_num);
  void pre_alloc_neuron_multi_cores(int net_idx, int stage_idx, const std::vector<int> &core_list);
  void pre_alloc_neuron_multi_thread(uint64_t thread_idx, const mem_info_t* mem_info);
  void pre_alloc_neuron(int net_idx);
  bool memcpy_s2d_parallel(bm_tensor_t tensors[], void * datas[],
                           int tensor_num[], int device_num);
  bool memcpy_d2s_parallel(void * datas[], bm_tensor_t tensors[],
                           int tensor_num[], int device_num);
  bool memcpy_d2d_byte_parallel(bm_tensor_t dst_tensors[], size_t dst_offsets[],
                                bm_tensor_t src_tensors[], size_t src_offsets[],
                                size_t sizes[], int tensor_num[], int device_num);
  bool memcpy_d2d_stride_ex_parallel(bm_tensor_t dst_tensors[],
                                    size_t dst_offsets[],
                                    bm_shape_t dst_strides[],
                                    bm_tensor_t src_tensors[],
                                    size_t src_offsets[],
                                    bm_shape_t src_strides[],
                                    bm_shape_t shapes[],
                                    int tensor_num[],
                                    int device_num);

  const bm_shape_t* get_input_max_shape(int net_idx, int input_idx);
  const bm_shape_t* get_output_max_shape(int net_idx, int output_idx);
  int get_input_blob_max_shape(const string& tensor_name, int net_idx, int* shape);
  int get_output_blob_max_shape(const string& tensor_name, int net_idx, int* shape);

  // get input and output index by name
  int get_input_index(const string& tensor_name, int net_idx);
  int get_output_index(const string& tensor_name, int net_idx);

  // data_type 0: FP32, 1: FP16, 2: INT8, 3: UINT8, 4: INT16, 5: UINT16
  int get_input_data_type(const string& tensor_name, int net_idx);
  int get_output_data_type(const string& tensor_name, int net_idx);

  // store mode 0: 1N, 1: 2N, 2: 4N
  int get_input_gmem_stmode(const string& tensor_name, int net_idx);
  int get_output_gmem_stmode(const string& tensor_name, int net_idx);

  /* COMMON */
  bool can_batch_size_change(int net_idx);
  bool can_height_and_width_change(int net_idx);

  /* simple get/show */
  void get_network_names(vector<const char*>* names);

  void show_neuron_network();

  /* flag get/set */
  inline uint32_t get_flags() {
    return m_flags;
  }

  inline void set_flags(uint32_t flags) {
    m_flags = flags;
  }

  inline int get_network_number()
  {
    auto num_cascade = m_net_cascade_v.size();
    auto num_net = m_net_ctx_v.size();
    if (num_cascade != 0) {
      for (auto v : m_net_ctx_v) {
        if (v->in_cascade) {
          num_net--;
        }
      }
    }
    return num_cascade + num_net;
  }

  inline bm_handle_t get_bm_handle(int device_idx=0)
  {
    return m_handles[device_idx];
  }
  inline int get_devid(int device_idx=0){
    return m_devids[device_idx];
  }
  const net_cascade_t* get_net_cascade(const string& net_name);
  bool cascade_thread_step(int net_idx,
                           std::vector<mem_cascade_t> *src,
                           std::vector<mem_cascade_t> *dst,
                           bm_handle_t m_handle);
  bool cascade_thread_global_move_data(
    int devid, bm_handle_t handle,
    std::vector<tpu_kernel_global_move_1684x_t> *param);
  int get_net_idx(const string& net_name);
  int get_stage_size(const string& net_name);
  const bm_net_info_t* get_net_info(int net_idx);
  const bm_net_info_t* get_net_info(const string& net_name);

  const vector<bm_device_mem_u64_t> &get_neuron_mem(int net_idx);
  void trace();

  size_t size_4N_align(const bm_shape_t& shape, const bm_data_type_t& type);

  u64 must_alloc_device_mem(uint32_t devid, bm_device_mem_t* mem, u64 size, const string& desc = "", int type_len=1);
  bm_device_mem_t must_alloc_device_mem(uint32_t devid, u64 size, const string& desc = "", int type_len=1);
  void must_free_device_mem(uint32_t devid, bm_device_mem_t& mem);

  // sg alloc for over 4GB
  u64 must_alloc_device_mem_u64(uint32_t devid, bm_device_mem_u64_t* mem, u64 size, const string& desc = "", int type_len=1);
  bm_device_mem_u64_t must_alloc_device_mem_u64(uint32_t devid, u64 size, const string& desc = "", int type_len=1);
  void must_free_device_mem_u64(uint32_t devid, bm_device_mem_u64_t& mem);

  u64 alloc_device_mem(uint32_t devid, bm_device_mem_t &mem, u64 size, const std::string &desc = "", int type_len=1, bool auto_free_mem=true);
  u64 alloc_device_mem_u64(uint32_t devid, bm_device_mem_u64_t &mem, u64 size, const std::string &desc = "", int type_len=1, bool auto_free_mem=true);
  bm_device_mem_t alloc_device_mem(uint32_t devid, u64 size, const std::string &desc = "", int type_len=1, bool auto_free_mem=true);
  bm_device_mem_u64_t alloc_device_mem_u64(uint32_t devid, u64 size, const std::string &desc = "", int type_len=1, bool auto_free_mem=true);
  void free_device_mem(uint32_t devid, bm_device_mem_t& mem);
  void free_device_mem_u64(uint32_t devid, bm_device_mem_u64_t& mem);
  void reset_device_mem(const std::string &desc);
  void clear_device_mem(const std::string &desc);
  void fill_dmem_info(int64_t addr, uint64_t size, const std::string &desc);
  int get_stage_idx(const char* net_name, const bm_tensor_t* input_tensors);

protected:
  bool alloc_mem;
  std::vector<device_mem_info_t> dmem_info;
  bool using_map;

 protected:
  void init();
  void init_bmfunc(const string& arch_name);
  void sync_cores(bm_handle_t handle, const std::vector<int32_t>& core_list);
  bool launch_static(net_ctx_t* net_ctx, net_stage_t* stage, const bm_tensor_t* input_tensors,
                     int input_num, bm_tensor_t* output_tensors, int output_num,
                     const std::vector<int32_t> &core_list, const size_t dyn_core_mask);
  bool launch_ir(net_ctx_t* net_ctx, net_stage_t* stage, const bm_tensor_t* input_tensors,
                 int input_num, bm_tensor_t* output_tensors, int output_num,
                 const std::vector<int32_t> &core_list, const size_t dyn_core_mask);

  int get_stage_idx(const net_ctx_t* net_ctx, const bm_tensor_t* input_tensors);
  int get_static_stage_idx(const net_ctx_t* net_ctx, const bm_tensor_t* input_tensors);
  int get_dynamic_stage_idx(const net_ctx_t* net_ctx, const bm_tensor_t* input_tensors);
  std::vector<int32_t> refine_core_list(const net_stage_t *stage,
                                        const std::vector<int32_t> &core_list,
                                        bm_handle_t handle);

protected:
  // functions for load bmodel
  u64 fix_gdma_addr(const net_stage_t* stage, u64 origin_addr, bool is_src);
  void convert_cmd(u32* cmd, int engine_id, bool last_cmd, u64 start_address,
                   const net_stage_t* stage);
  bool setup_cmd_context(ModelCtx* model_ctx, const bmodel::NetParameter *param,
                         net_stage_t* stage, uint32_t device_id);
  bool setup_ir_context(ModelCtx* model_ctx, const bmodel::Binary* binary_ir,
                        const Vector<Offset<bmodel::StageIR>>* stage_ir, net_stage_t* stage, uint32_t device_id);
  bool load_bmodel(ModelCtx*);
  bool load_bmodel_net(ModelCtx*, int net_idx);
  bool load_bmodel_net(ModelCtx*, int net_idx, net_ctx_t* net_ctx);
  bool cascade_net_init(const Net* net, int net_idx, net_ctx_t* net_ctx);
  void load_tpu_module(ModelCtx*);
  void load_cpu_module(ModelCtx*);
  bool fill_net_ctx(
      ModelCtx* model_ctx,
      net_ctx_t* net_ctx, const Vector<Offset<NetParameter>>* params,
      vector<vector<u64>> &stage_ctx_sizes, net_stage_t *stages);
  void fill_subnet_dyn_neuron_tensor(
      net_ctx_t* net_ctx, const size_t dyn_core_mask,
      const net_stage_t *common_stage_info);
  void net_ctx_alloc_dyn_neuron(net_ctx_t* net_ctx, const size_t dyn_core_mask,
      const net_stage_t *common_stage_info, bool use_multi_subnet);
  void net_ctx_alloc_dyn_neuron(net_ctx_t* net_ctx, const size_t thread_id, const size_t dyn_core_mask);
  void update_dyn_neuron(net_ctx_t* net_ctx, const size_t thread_id, const net_stage_t *common_stage_info);
  void fill_net_info(net_ctx_t* net_ctx);
  void free_net_info(net_ctx_t* net_ctx);
  void free_dyn_neuron(net_ctx_t* net_ctx);
  void update_net_middlebuf(net_ctx_t *net_ctx);
  void update_max_middlebuf_size(net_ctx_t* net_ctx);
  void update_max_neuron_mem(uint32_t devid, const vector<u64> &sizes);
  bool setup_profile_context(ModelCtx* model_ctx, net_stage_t* net_stage,
                             const bmodel::Binary* net_profile,
                             const bmodel::Binary* net_stat);

  void set_profile_enabled(bool enable);
  bool update_net_coeff(
    ModelCtx* model_ctx, int net_idx, int mem_idx,
    const std::vector<int> &weight_idx, const std::vector<uint8_t> &file_data, long long &start_position);
  bool update_bmodel_coeff(
    ModelCtx* model_ctx, const std::string &update_path, const std::vector<int> &net_idx,
    const std::vector<int> &mem_idx, const std::vector<std::vector<int>> &weight_idx);
  bool update_bmodel_coeff(
    ModelCtx* model_ctx, const std::vector<int> &net_idx, const std::vector<int> &mem_idx,
    const std::vector<std::vector<int>> &weight_idx, const std::vector<uint8_t> &file_data, long long &start_position);

  // functions for fill static bmdnn net info
  void fill_tpu_net_info(net_ctx_t *net_ctx, net_stage_t *stage,
                         const bm_tensor_t *input_tensors, int input_num,
                         bm_tensor_t *output_tensors, int output_num,
                         const std::vector<int32_t> &core_list,
                         tpu_net_info_t &net_info,
                         const size_t dyn_core_mask);
  template <typename T_stage>
  void fill_tpu_tensor_info(vector<tpu_tensor_info_t> &tensor_info,
                            const T_stage *stage,
                            const bm_tensor_t *user_tensors, bool is_input);
  // function for fill tpu static subnet net info
  template <typename T_stage>
  void fill_tpu_tensor_info(vector<tpu_tensor_info_t> &tensor_info,
                            const T_stage *stage,
                            const SUBNET_INFO_T *subnet,
                            const bm_tensor_t *user_tensors, bool is_input);
  void fill_tpu_cmd_info(std::vector<tpu_cmd_info_t> &cmd_info,
                         const std::vector<single_core_command_t> &core_commands,
                         const int32_t core_idx);
  template <typename T_stage>
  void get_tensor_attr(const T_stage *stage, const vector<string> &tensor_name_v,
                       vector<tensor_attr_t> &input_v);
  void fill_tpu_tensor_info(vector<tpu_tensor_info_t> &tensor_info,
    const vector<tensor_attr_t> &tensor_v, const bm_tensor_t *user_tensors,
    bool is_input);
  // functions for cascade
  void cascade_fill_net_info(net_cascade_t *net_cascade);
  void cascade_free_net_info(net_cascade_t *net_cascade);
  bool cascade_insert_net(int net_idx, net_ctx_t *net_ctx,
                          const string &main_name);
  void cascade_update_all_info();
  void cascade_update_input(net_cascade_t &v);
  void cascade_update_output(net_cascade_t &v);
  void cascade_update_max_hidden_buffer_size(net_cascade_t &v);
  void cascade_update_hidden_buffer(net_cascade_t &v);
  bm_tensor_t *
  cascade_prepare_input(const string &name,
                        int32_t devid,
                        std::vector<mem_cascade_t> *src,
                        std::vector<mem_cascade_t> *dst);
  bm_tensor_t *
  cascade_prepare_output(const string &name, uint32_t devid,
                         std::vector<mem_cascade_t> *dst);

  bool cascade_update_output_shape(net_ctx_t *net_ctx,
                                  std::vector<mem_cascade_t> *dst,
                                  std::vector<bm_tensor_t> out_tensors);
  uint32_t get_dyn_core_mask(int stage_idx, const std::vector<int32_t> core_list);
  std::vector<int> get_core_list_from_core_mask(uint32_t dyn_core_mask);
public:
  api_info_t get_api_info(int net_idx, const bm_tensor_t *input_tensors,
                          int input_num, bm_tensor_t *output_tensors,
                          int output_num, bool user_mem, bool user_stmode,
                          uint32_t *core_ids);

protected:                                                    // one bmruntime can load nets at most
  vector<net_ctx_t*> m_net_ctx_v;
  vector<net_cascade_t> m_net_cascade_v;                      // net in cascade info
  vector<std::shared_ptr<CascadeThread>> m_cascade_thread_v;  // thread for cascade

  static const int MAX_DEVICE_NUM = 32;   // one bmruntime can run 32 device at most
  bm_handle_t m_handles[MAX_DEVICE_NUM];
  int m_device_num;
  unsigned int m_core_num;
  bool using_internal_hidden_tensors; /* internal initlized hidden_tensors device_mem or accept from user parameter when launch */
  bool using_internal_bm_handle; /* internal initlized bm_handle or accept from user parameter */
  int m_devids[MAX_DEVICE_NUM];
  bool using_fast_allreduce;

  vector<bm_device_mem_t> m_device_mem_vec;     /* save device memory address, for free */
  vector<uint32_t> m_device_mem_ids;            /* record each device memory belong which device*/

  vector<bm_device_mem_u64_t> m_sg_device_mem_vec;     /* save device memory address, for free */
  vector<uint32_t> m_sg_device_mem_ids;            /* record each device memory belong which device*/

  std::shared_ptr<BmCoeff> m_local_coeffs[MAX_DEVICE_NUM];
  static map<int, std::shared_ptr<BmCoeff>> m_global_coeff_map;
  static std::mutex m_global_coeff_mutex;

  static map<vector<u8>, std::unique_ptr<uint8_t[]>> m_global_cpu_const_map;
  static std::mutex m_global_cpu_const_mutex;

  std::mutex m_load_mutex;

  bool b_enable_mmap;
  bool m_subnet_time_print;
  uint32_t m_flags;

  std::shared_ptr<BMProfile> m_profile;

  // For middle buffer
  // Because max_middle_buffer is also record in m_device_mem_vec.
  // So we do not need to free max_middle_buffer at last.
  bm_device_mem_t max_middle_buffer[MAX_DEVICE_NUM];
  u64 max_middle_buffer_size[MAX_DEVICE_NUM];
  u32 middle_buffer_num[MAX_DEVICE_NUM];

  // For hidden buffer
  // Because max_hidden_buffer is also record in m_device_mem_vec.
  // So we do not need to free max_hidden_buffer at last.
  bm_device_mem_t max_hidden_buffer[MAX_DEVICE_NUM];
  u64 max_hidden_buffer_size[MAX_DEVICE_NUM];
  u32 hidden_buffer_num[MAX_DEVICE_NUM];

  // For neuron memory share
  u32 m_neuron_heap_mask;
  vector<bm_device_mem_u64_t> max_neuron_mem[MAX_DEVICE_NUM];
  vector<u64> max_neuron_mem_size[MAX_DEVICE_NUM];
  std::shared_ptr<KernelModule> kernel_modules[MAX_DEVICE_NUM];

 protected:
  /* functions for subnet */
  void bmcpu_setup();
  void bmtpu_setup();
  bool launch_cpu_subnet(net_ctx_t* net_ctx, map<string, tensor_ext_t> *subnet_tensor_v, const SUBNET_INFO_T* subnet,
                         const bm_tensor_t* input_tensors, bm_shape_t real_out_shape[]);
  bool launch_tpu_subnet(net_ctx_t* net_ctx, net_stage_t* stage, const SUBNET_INFO_T* subnet,
                         const bm_tensor_t* input_tensors, int input_num,
                         bm_tensor_t* output_tensors, int output_num,
                         const std::vector<int32_t> &core_list, const uint32_t dyn_core_mask, bool force_sync);
  bool launch_tpu_ir_subnet(net_ctx_t* net_ctx, net_stage_t* stage, const SUBNET_INFO_T* subnet,
                            const bm_tensor_t* input_tensors, const int* input_elem_num, int input_num,
                            bm_tensor_t* output_tensors, int* output_elem_num, int output_num,
                            const std::vector<int32_t> &core_list, const uint32_t dyn_core_mask);
  bool launch_multi_subnet(net_ctx_t* net_ctx, net_stage_t* stage, const bm_tensor_t* input_tensors,
                           int input_num, bm_tensor_t* output_tensors, int output_num,
                           const std::vector<int32_t> &core_list, const uint32_t dyn_core_mask);
  void fill_sub_net(ModelCtx* model_ctx, const Vector<Offset<bmodel::SubNet>>* subnet_set_v,
                    net_ctx_t* net_ctx, net_stage_t* net_stage);
  void fill_subnet_tensor_map(net_ctx_t* net_ctx, net_stage_t* net_stage, SUBNET_INFO_T* subnet,
                              const Vector<Offset<bmodel::Tensor>>* tensor_set_v, bool is_input,
                              std::set<string> subnet_switch_inputs);
  void subnet_clear(net_ctx_t* net_ctx);
  void subnet_tensor_s2d(uint32_t devid, map<string, tensor_ext_t> *subnet_tensor_v, const string& tensor_name,
                         bm_device_mem_t* out_dev_mem = NULL, u64 offset = 0, u64 size = 0);
  void* subnet_tensor_d2s(uint32_t devid, map<string, tensor_ext_t> *subnet_tensor_v, const string& tensor_name,
                         bm_device_mem_t* out_dev_mem = NULL, u64 offset = 0, u64 size = 0);
  void subnet_tensor_forward(uint32_t devid, map<string, tensor_ext_t> *subnet_tensor_v, const string& src_tensor, const string& dst_tensor, const bm_tensor_t* output_tensors);

 protected:
  typedef void* (*t_bmcpu_init)();
  typedef void (*t_bmcpu_uninit)(void*);
  typedef void (*t_bmcpu_process)(void*, int, void*, int, const vector<float*>&,
                                  const vector<vector<int>>&, const vector<float*>&,
                                  vector<vector<int>>&);
  void* bmcpu_handle_;
  t_bmcpu_init bmcpu_init_;
  t_bmcpu_uninit bmcpu_uninit_;
  t_bmcpu_process bmcpu_process_;

  void* customcpu_handle_ = NULL;
  t_bmcpu_init customcpu_init_ = NULL;
  t_bmcpu_uninit customcpu_uninit_ = NULL;
  t_bmcpu_process customcpu_process_ = NULL;

  std::shared_ptr<KernelModule> kernel_module_;

 private:
  bmfunc* p_bmfunc;

  // temp custom cpu related
  void *tmpcpuso_handle_ = NULL;
  std::string temp_filename_;
  int card_chip_num;
};

class BmCoeff {
 public:
  explicit BmCoeff(int devid);
  ~BmCoeff();

  u64 Register(ModelCtx* model_ctx, const CoeffMem* coeff_mem);
  u64 Register(ModelCtx* model_ctx, const CoeffMem* coeff_mem, int64_t addr);
  u64 Update(
    ModelCtx* model_ctx, const CoeffMem* coeff_mem, int mem_idx,
    const std::vector<int> &weight_idx, const std::vector<uint8_t> &file_data, long long &start_position);
  int64_t GetCoeffAddr(const CoeffMem* coeff_mem);
  int Check();
  bm_device_mem_u64_t GetCoeffDeviceMem() {
    return m_latest_device_mem;
  }

 protected:
  map<vector<u8>, bm_device_mem_u64_t> m_coeff_map; /* to share the same coeff, by check code*/
  std::mutex m_coeff_mutex;
  bm_handle_t m_handle = NULL;
  int m_devid = -1;
  bm_device_mem_u64_t m_latest_device_mem;
  std::vector<bm_device_mem_u64_t> m_mem_need_free;
};

class KernelModule {
public:
  explicit KernelModule(bm_handle_t &handle):m_handle(handle) {}
  ~KernelModule();
private:
  void preload_funcs(int core_id);
public:
  void add_core_module(int core_id, const unsigned char* binary, size_t size);
  void add_core_module(int core_id, const char* filename);
  vector<tpu_kernel_function_t> get_multi_fullnet_func_id(const vector<int>& core_list);
  vector<tpu_kernel_function_t> get_dynamic_fullnet_func_id(const vector<int>& core_list);
  vector<tpu_kernel_function_t> get_enable_profile_func_id(const vector<int>& core_list);
  vector<tpu_kernel_function_t> get_get_profile_func_id(const vector<int>& core_list);
  vector<tpu_kernel_function_t> get_set_engine_profile_param_func_id(const vector<int>& core_list);
  vector<tpu_kernel_function_t> get_global_move_1684x_func_id(const vector<int>& core_list);

private:
  bm_handle_t m_handle;
  map<int, tpu_kernel_module_t> _kernel_modules;
  map<int, tpu_kernel_function_t> _multi_fullnet_func_id;
  map<int, tpu_kernel_function_t> _dynamic_fullnet_func_id;
  map<int, tpu_kernel_function_t> _enable_profile_func_id;
  map<int, tpu_kernel_function_t> _get_profile_func_id;
  map<int, tpu_kernel_function_t> _set_engine_profile_param_func_id;
  map<int, tpu_kernel_function_t> _global_move_1684x_func_id;
  vector<tpu_kernel_function_t> __get_vector_funcs(const vector<int>& core_list, map<int, tpu_kernel_function_t>& func_map, const char* name);
};

class CascadeThread {
  typedef enum {
    NET_MODE = 0,
    S2D_MODE = 1,
    D2S_MODE = 2,
    D2D_MODE = 3,
    D2D_STRIDE_EX_MODE = 4,
    UNKNOWN = -1,
  } FUNC_MODE_T;
public:
  CascadeThread(Bmruntime *rt, bm_handle_t handle)
      : m_stop(false), m_paramReady(false), m_done(true),
        m_ok(true), m_handle(handle), m_rt(rt)
        {
    m_worker = std::thread(&CascadeThread::threadFunction, this);
  }

  ~CascadeThread() {
    {
      // std::unique_lock<std::mutex> lock(m_mutex);
      m_done = false;
      m_stop = true;
      // m_condition.notify_all();
      while(m_done == false) {std::this_thread::yield();}
    }
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

  void run(int net_idx,
           vector<mem_cascade_t> *src,
           vector<mem_cascade_t> *dst) {
    // std::unique_lock<std::mutex> lock(m_mutex);
    m_net_idx = net_idx;
    m_src = src;
    m_dst = dst;
    m_mode = NET_MODE;
    m_done = false;
    m_paramReady = true;
    // m_condition.notify_all();
  }

  void s2d(bm_tensor_t *tensors, void **datas, int tensor_num) {
    // std::unique_lock<std::mutex> lock(m_mutex);
    m_dst_tensors = tensors;
    m_datas = datas;
    m_mode = S2D_MODE;
    m_tensor_num = tensor_num;
    m_done = false;
    m_paramReady = true;
    // m_condition.notify_all();
  }

  void d2s(void **datas, bm_tensor_t *tensors, int tensor_num) {
    // std::unique_lock<std::mutex> lock(m_mutex);
    m_src_tensors = tensors;
    m_datas = datas;
    m_mode = D2S_MODE;
    m_tensor_num = tensor_num;
    m_done = false;
    m_paramReady = true;
    // m_condition.notify_all();
  }

  void d2d(bm_tensor_t *dst_tensors, size_t *dst_offsets,
           bm_tensor_t *src_tensors, size_t *src_offsets,
           size_t *sizes, int tensor_num) {
    // std::unique_lock<std::mutex> lock(m_mutex);
    m_src_tensors = src_tensors;
    m_dst_tensors = dst_tensors;
    m_src_offsets = src_offsets;
    m_dst_offsets = dst_offsets;
    m_sizes = sizes;
    m_mode = D2D_MODE;
    m_tensor_num = tensor_num;
    m_done = false;
    m_paramReady = true;
    // m_condition.notify_all();
  }

  void d2d_stride_ex(int devid,
                    std::vector<tpu_kernel_global_move_1684x_t> *params) {
    // std::unique_lock<std::mutex> lock(m_mutex);
    m_devid = devid;
    m_global_move_params = params;
    m_mode = D2D_STRIDE_EX_MODE;
    m_done = false;
    m_paramReady = true;
    // m_condition.notify_all();
  }

  bool sync() {
    // std::unique_lock<std::mutex> lock(m_mutex);
    // m_doneCondition.wait(lock, [this]() { return m_done; });
    while(m_done == false) {std::this_thread::yield();}
    return m_ok;
  }

private:
  void threadFunction() {
    while (true) {
      // std::unique_lock<std::mutex> lock(m_mutex);
      // m_condition.wait(lock, [this]() { return m_paramReady || m_stop; });
      // BMRT_LOG(INFO, "M_MODE is %d\n", m_mode);
      while (m_paramReady == false && m_stop == false) {std::this_thread::yield();}
      if (m_stop) {
        m_done = true;
        return;
      }
      if (m_mode == NET_MODE) {
        m_ok = m_rt->cascade_thread_step(m_net_idx, m_src, m_dst, m_handle);
        if (m_ok) {
          auto status = bm_thread_sync(m_handle);
          m_ok = BM_SUCCESS == status;
        }
      } else if (m_mode == S2D_MODE) {
        for (int i = 0; i < m_tensor_num; ++i) {
          auto status = bm_memcpy_s2d(m_handle, m_dst_tensors[i].device_mem, m_datas[i]);
          if (BM_SUCCESS != status) {
            m_ok = false;
            break;
          } else {
            m_ok = true;
          }
        }
      } else if (m_mode == D2S_MODE) {
        for (int i = 0; i < m_tensor_num; ++i) {
          auto status = bm_memcpy_d2s(m_handle, m_datas[i], m_src_tensors[i].device_mem);
          if (BM_SUCCESS != status) {
            m_ok = false;
            break;
          } else {
            m_ok = true;
          }
        }
      } else if (m_mode == D2D_MODE) {
        for (int i = 0; i < m_tensor_num; ++i) {
          auto status = bm_memcpy_d2d_byte(m_handle, m_dst_tensors[i].device_mem, m_dst_offsets[i],
                                          m_src_tensors[i].device_mem, m_src_offsets[i], m_sizes[i]);
          if (BM_SUCCESS != status) {
            m_ok = false;
            break;
          } else {
            m_ok = true;
          }
        }
      } else if (m_mode == D2D_STRIDE_EX_MODE) {
        // global move
        m_ok = m_rt->cascade_thread_global_move_data(m_devid, m_handle,
                                                    m_global_move_params);
      }
      m_paramReady = false;
      m_done = true;
      // m_doneCondition.notify_one();
    }
  }

  std::thread m_worker;
  // std::mutex m_mutex;
  // std::condition_variable m_condition;
  // std::condition_variable m_doneCondition;
  std::atomic_bool m_stop;
  std::atomic_bool m_paramReady;
  std::atomic_bool m_done;
  // bool m_stop;
  // bool m_paramReady;
  // bool m_done;
  bool m_ok;
  // s2d/d2s/d2d param
  bm_tensor_t *m_src_tensors;
  bm_tensor_t *m_dst_tensors;
  void **m_datas;
  FUNC_MODE_T m_mode;
  int m_tensor_num;
  size_t *m_src_offsets;
  size_t *m_dst_offsets;
  size_t *m_sizes;
  // net param
  int m_net_idx;
  bm_handle_t m_handle;
  Bmruntime *m_rt;
  vector<mem_cascade_t> *m_src;
  vector<mem_cascade_t> *m_dst;
  // d2d_stride_ex param
  int m_devid;
  std::vector<tpu_kernel_global_move_1684x_t> *m_global_move_params;
};

}  // namespace bmruntime

#endif
