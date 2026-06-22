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
 * These APIs are old bmtap2 c++ inference, not suggested to be used in the future.
 * Only support static nets, and only load one net in multi-net bmodel.
 * Suggest to use API in bmruntime_cpp.h
 ****************************************************************************/

#ifndef LIBBMRUNTIME_BMRUNTIME_HPP_
#define LIBBMRUNTIME_BMRUNTIME_HPP_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <bmruntime_bmnet.h>

namespace bmruntime {
class BmCtx;
class Blob;
class Net;

class Blob {
 public:
  Blob();

  /**
   * @brief construct a Blob
   *
   * @param [in] data  the start address of buffer
   * @param ]in] shape N C H W
   * @param [in] fmt   data format, such as FMT_F32, FMT_I8, etc.
   */
  Blob(void* data, shape_t& shape, uint32_t fmt = FMT_F32);
  ~Blob();

  /**
   * @brief  get the data of blob
   *
   * @retval the start address of data
   */
  void* data() const;

  /**
   * @brief  get the shape of blob
   *
   * @retval a shapt_t structure
   */
  const shape_t& shape();

  /**
   * @brief  get the data format of blob
   *
   * @retval FMT_F32, FMT_I8, etc.
   */
  uint32_t fmt() const;

  /**
   * @brief  get the number of elements in blob
   *
   * @retval N * C * H * W
   */
  size_t count() const;

  /**
   * @brief  get size in bytes of data buffer
   *
   * @retval size of data buffer in bytes
   */
  size_t size() const;

  /**
   * @brief dump blob information
   */
  void dump() const;

 private:
  void* data_;
  shape_t shape_;
  uint32_t fmt_;
  size_t count_;
  size_t size_;
};  // class Blob

class Net {
 public:
  /**
   * @brief construct a Net with the specified bmodel file
   *
   * @param [in] bmodel  the name of bmodel file
   */
  Net(const std::string& bmodel);

  virtual ~Net();

  /**
   * @brief run the inference with the specified input blobs.
   *
   * @param [in] input_blobs  A vector stored with input blob objects.
   *
   * @retval 0        on success
   * @retval negative on failure
   */
  int forward(std::vector<Blob>& input_blobs);

  /**
   * @brief get the specified output blob
   *
   * @param [in] output_name such as "conv4-2"
   *
   * @retval Blob pointer on success
   * @retval nullptr      on failure
   */
  Blob* output(const std::string& output_name);

  /**
   * @brief get net model info
   *
   * @retval bmnet_model_info_t *    model info structure
   */
  const bmnet_model_info_t* info();

 private:
  std::shared_ptr<BmCtx> bmctx_instance_;
  bmnet_t net_;
  uint8_t* output_buffer_;
  std::string input_shape_;
  std::map<std::string, std::map<std::string, Blob>> blobs_;
};  // class Net

}  // namespace bmruntime

#endif
