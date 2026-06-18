#ifndef BMCV_API_H
#define BMCV_API_H
#include "bmlib_runtime.h"
#include "bmcv_api_ext.h"

/**
 * Abandoned macro definitions and interface, supports compatibility settings
 * Not recommended for use
*/

static inline bm_status_t bm_image_destroy(bm_image &image){
    return bm_image_destroy(&image);
}

static inline bm_status_t bmcv_fft_1d_create_plan(bm_handle_t handle, int batch, int len,
                                                bool forward, void *&plan) {
    return bmcv_fft_1d_create_plan(handle, batch, len, forward, &plan);
}

static inline bm_status_t bmcv_fft_2d_create_plan(bm_handle_t handle, int M, int N,
                                                bool forward, void *&plan) {
    return bmcv_fft_2d_create_plan(handle, M, N, forward, &plan);
}

#if defined (__cplusplus)
extern "C" {
#endif

typedef bmcv_padding_attr_t bmcv_padding_atrr_t;

DECL_EXPORT bm_status_t bmcv_image_crop(
    bm_handle_t         handle,
    int                 crop_num,
    bmcv_rect_t *       rects,
    bm_image            input,
    bm_image *          output);

DECL_EXPORT bm_status_t bm_image_dev_mem_alloc(bm_image image, int heap_id);

DECL_EXPORT bm_status_t bm_image_dettach_contiguous_mem(int image_num, bm_image *images);

DECL_EXPORT bm_status_t bmcv_image_yuv2bgr_ext(
    bm_handle_t handle,
    int         image_num,
    bm_image *  input,
    bm_image *  output);

#if defined (__cplusplus)
}
#endif

#endif /* BMCV_API_H */