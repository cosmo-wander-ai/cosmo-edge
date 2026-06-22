/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2023. All rights reserved.
 *
 * File Name: include/tde.h
 * Description:
 *   tde interfaces.
 */

#ifndef TDE_H
#define TDE_H

#include "comm_tde.h"
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

/* API Declaration */
int tde_open(void);
void tde_close(void);
int tde_begin_job(void);
int tde_end_job(int handle, bool is_sync, bool is_block, unsigned int time_out);
int tde_cancel_job(int handle);
int tde_wait_the_task_done(int handle);
int tde_wait_all_task_done(void);
int tde_quick_fill(int handle, const tde_none_src *none_src, unsigned int fill_data);
int tde_draw_corner_box(int handle, const tde_surface *dst_surface, const tde_corner_rect *corner_rect,
    unsigned int num);
int tde_draw_line(int handle, const tde_surface *dst_surface, const tde_line *line, unsigned int num);
int tde_quick_copy(int handle, const tde_single_src *single_src);
int tde_quick_resize(int handle, const tde_single_src *single_src);
int tde_solid_draw(int handle, const tde_single_src *single_src, const tde_fill_color *fill_color,
    const tde_opt *opt);
int tde_rotate(int handle, const tde_single_src *single_src, tde_rotate_angle rotate);
int tde_bit_blit(int handle, const tde_double_src *double_src, const tde_opt *opt);
int tde_pattern_fill(int handle, const tde_double_src *double_src,
    const tde_pattern_fill_opt *fill_opt);
int tde_mb_blit(int handle, const tde_mb_src *mb_src, const tde_mb_opt *opt);
int tde_set_alpha_threshold_value(unsigned char threshold_value);
int tde_get_alpha_threshold_value(unsigned char *threshold_value);
int tde_set_alpha_threshold_state(bool threshold_en);
int tde_get_alpha_threshold_state(bool *threshold_en);
int save_bmp(const char *image_name, unsigned char *p, unsigned int width, unsigned int height,
            unsigned int stride, tde_color_format tde_format);
int save_raw(const char *name, unsigned char* buffer, unsigned int width, unsigned int height,
            unsigned int stride, tde_color_format tde_format);

#ifdef __cplusplus
}
#endif

#endif /* __TDE_API__ */
