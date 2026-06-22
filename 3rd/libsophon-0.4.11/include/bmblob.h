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

#ifndef __BM_BLOB_H__
#define __BM_BLOB_H__

#include "bmdef.h"

struct bm_mem_desc;
typedef struct bm_mem_desc bm_device_mem_t;
namespace bmcnn {

#define MAX_SHAPE_DIMS 8
struct Shape {
    union{
        struct{
            int n,c,h,w;
        };
        struct {
            int data[MAX_SHAPE_DIMS];
        };
    };
    int dims;
    Shape(){
        dims = 4;
        for(int i=0; i<MAX_SHAPE_DIMS; i++){
            data[i] = 1;
        }
    }
};

class BMBlob
{
public:
    /**
     * \brief Constructor of blob.
     *
     * \param shape - Shape of blob
     */
    explicit BMBlob(const Shape &shape, const bm_data_type_t type, void *handle);
    /**
     * \brief Deconstructor of blob.
     */
    virtual ~BMBlob();
    /**
     * \brief Reshape blob.
     *
     * \param n - Batch number of blob
     * \param c - Channel number of blob
     * \param h - Height of blob section
     * \param w - Width of blob section
     *
     * \note
     * (1) For now, number of channels is not allowed to be reshaped.\n
     * (2) After reshaping, data in this blob will be set vanished.\n
     */
    void Reshape(int n, int c, int h, int w);
    void Reshape(const int* shape_data, int len);
    void Reshape(const Shape& s);
    /**
     * \brief Syncronize the shape
     *
     * \note
     * Only change the shape value. The data is not vanished.\n
     */
    void SyncShape(int n, int c, int h, int w);
    void SyncShape(const Shape& s);
    void SyncShape(const int* shape_data, int len);
    /**
     * \brief Get shape.
     */
    inline Shape shape() const { return shape_; }
    /**
     * \brief Get batch size.
     */
    inline int batch_num() const { return shape_.dims>0?shape_.data[0]:1; }
    /**
     * \brief Get feature
     *
     * \return Channel number of the blob\n
     */
    inline int channels() const { return shape_.dims>1?shape_.data[1]:1; }
    /**
     * \brief Get height of section
     */
    int height() const { return shape_.dims>2?shape_.data[2]:1; }
    /**
     * \brief Get width of section.
     */
    int width() const {
        int w = 1;
        for(int i=3; i<shape_.dims; i++){
            w*=shape_.data[i];
        }
        return w;
    }

    int dims() const { return shape_.dims; }
    /**
     * \brief general shape size by dim
     */
    int shape_size(int dim) const {
        return dim<shape_.dims?shape_.data[dim]:1;
    }

    /**
     * \data length for each batch
     */
    int batch_length() const {
        int num = 1;
        for(int i=1; i<shape_.dims; i++){
            num *= shape_.data[i];
        }
        return num;
    }

    /**
     * \brief total element number in the blob
     */
    int num_elements() const {
        return batch_num() * batch_length();
    }

    /**
     * \brief Get read-only pointer to data in cpu.
     */
    const void *cpu_data();
    /**
     * \brief Get mutable pointer of data in cpu.
     */
    void *mutable_cpu_data();
    /**
     * \brief Get mutable pointer of memory in device.
     */
    bm_device_mem_t *mutable_dev_mem();
    /**
     * \brief Get read-only pointer of memory in device.
     */
    const bm_device_mem_t *dev_mem();
    /**
     * map device mem to user space virtual mem
     */

    void *mutable_mapped_output();
    void fill_cpu_data(void * in_data, int num_element);
    void dump_cpu_data(void * out_data, int num_element);

    /**
     * \brief Get memory store mode
     */
    int store_mode() {return st_mode_;}

    bm_data_type_t data_type() {return data_type_;}
    /**
     * \brief Set memory store mode
     */
    void set_store_mode(int stmode);
private:
    BMBlob(const BMBlob &other);
    BMBlob &operator=(const BMBlob &other);

    void *handle_;

    Shape shape_;
    int capacity_;
    size_t capacity_size_;

    enum { AIR = 0x00, SYS = 0x01, DEV = 0x10 };
    int data_pos_;  /* data is in sys/dev/air */
    void *sys_data_;
    bm_device_mem_t *dev_mem_;
    int st_mode_; //0: 1N, 1: 2N, 2: 4N
    bm_data_type_t data_type_;
    size_t data_type_size_;
    int batch_unit_;

    void *mapped_data_;

    void sync_s2d();
    void sync_d2s();
};

} /* namespace bmcnn */

#endif /* __BM_BLOB_H__ */
