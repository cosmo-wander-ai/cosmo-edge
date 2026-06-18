/*
 * Copyright (c) 2014 Tim Walker <tdskywalker@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
 
 /*****************************************************************************
  * NAME		 : hevc.cpp
  * FUNCTION	 : 解析h265的参数集数据
  * PROGRAMMED	 : 陈文敏(chenwenmin)
  * DATE		 : 2015-12-08
  * PROJECT 	 : 
  * OS			 : Linux
  ****************************************************************************/ 

#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/hevc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/log.h"
#include "libavformat/avc.h"
#include "libavformat/avio.h"
#include "libavformat/avio.h"

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field

#if 1
//cwm libavutil/log2_tab.c
#include <stdint.h>
extern "C"
{

const uint8_t ff_log2_tab[256]={
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};
//cwm libavutil/log2_tab.c
#endif 

#if 1
//cwm_add libavcodec/golomb.c
#include "libavutil/common.h"

const uint8_t ff_golomb_vlc_len[512]={
19,17,15,15,13,13,13,13,11,11,11,11,11,11,11,11,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

const uint8_t ff_ue_golomb_vlc_code[512]={
32,32,32,32,32,32,32,32,31,32,32,32,32,32,32,32,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,
 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9,10,10,10,10,11,11,11,11,12,12,12,12,13,13,13,13,14,14,14,14,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const int8_t ff_se_golomb_vlc_code[512]={
 17, 17, 17, 17, 17, 17, 17, 17, 16, 17, 17, 17, 17, 17, 17, 17,  8, -8,  9, -9, 10,-10, 11,-11, 12,-12, 13,-13, 14,-14, 15,-15,
  4,  4,  4,  4, -4, -4, -4, -4,  5,  5,  5,  5, -5, -5, -5, -5,  6,  6,  6,  6, -6, -6, -6, -6,  7,  7,  7,  7, -7, -7, -7, -7,
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};


const uint8_t ff_ue_golomb_len[256]={
 1, 3, 3, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,11,
11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,13,
13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,17,
};

const uint8_t ff_interleaved_golomb_vlc_len[256]={
9,9,7,7,9,9,7,7,5,5,5,5,5,5,5,5,
9,9,7,7,9,9,7,7,5,5,5,5,5,5,5,5,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
9,9,7,7,9,9,7,7,5,5,5,5,5,5,5,5,
9,9,7,7,9,9,7,7,5,5,5,5,5,5,5,5,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

const uint8_t ff_interleaved_ue_golomb_vlc_code[256]={
 15,16,7, 7, 17,18,8, 8, 3, 3, 3, 3, 3, 3, 3, 3,
 19,20,9, 9, 21,22,10,10,4, 4, 4, 4, 4, 4, 4, 4,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 23,24,11,11,25,26,12,12,5, 5, 5, 5, 5, 5, 5, 5,
 27,28,13,13,29,30,14,14,6, 6, 6, 6, 6, 6, 6, 6,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

const int8_t ff_interleaved_se_golomb_vlc_code[256]={
  8, -8,  4,  4,  9, -9, -4, -4,  2,  2,  2,  2,  2,  2,  2,  2,
 10,-10,  5,  5, 11,-11, -5, -5, -2, -2, -2, -2, -2, -2, -2, -2,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
 12,-12,  6,  6, 13,-13, -6, -6,  3,  3,  3,  3,  3,  3,  3,  3,
 14,-14,  7,  7, 15,-15, -7, -7, -3, -3, -3, -3, -3, -3, -3, -3,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

const uint8_t ff_interleaved_dirac_golomb_vlc_code[256]={
0, 1, 0, 0, 2, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
4, 5, 2, 2, 6, 7, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
8, 9, 4, 4, 10,11,5, 5, 2, 2, 2, 2, 2, 2, 2, 2,
12,13,6, 6, 14,15,7, 7, 3, 3, 3, 3, 3, 3, 3, 3,
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,};

//cwm_add libavcodec/golomb.c
#endif


#if 1
//cwm  libavutil/intmath.c
#include "libavutil/intmath.h"

/* undef these to get the function prototypes from common.h */
#undef av_log2
#undef av_log2_16bit
#include "libavutil/common.h"

int av_log2(unsigned v)
{
    return ff_log2(v);
}

int av_log2_16bit(unsigned v)
{
    return ff_log2_16bit(v);
}
//cwm  libavutil/intmath.c
#endif

#if 1
//cwm libavformat/avc.c

#include "libavutil/intreadwrite.h"
#include "libavformat/avio.h"
#include "libavformat/avc.h"

static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

const uint8_t *ff_avc_find_startcode(const uint8_t *p, const uint8_t *end){
    const uint8_t *out= ff_avc_find_startcode_internal(p, end);
    if(p<out && out<end && !out[-1]) out--;//cwm 不执行此句，会使out的值大于1，造成生成的mp4文件中nalu的末尾多一个字节
    return out;
}
//解析出nal的数据保存到了pb->buf_ptr, buf_ptr代表的是当前的可写buf的位置，起始位置pb->buffer
int ff_avc_parse_nal_units(AVIOContext *pb, const uint8_t *buf_in, int size)
{
    const uint8_t *p = buf_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start, *nal_end;

    size = 0;
    nal_start = ff_avc_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));//去掉开始位0x00000001, nal_start+=4,nal_start指向nal单元的开始
        if (nal_start == end)
            break;

        nal_end = ff_avc_find_startcode(nal_start, end);//cwm if(nal_end==end)时，计算出的nal_end没有减1，造成多写1个字节
		*((uint32_t *)pb->buf_ptr) = ((nal_end - nal_start)>>24) & 0xff;
		pb->buf_ptr += 1;
		*((uint32_t *)pb->buf_ptr) = ((nal_end - nal_start)>>16) & 0xff;
		pb->buf_ptr += 1;
		*((uint32_t *)pb->buf_ptr) = ((nal_end - nal_start)>>8) & 0xff;
		pb->buf_ptr += 1;
		*((uint32_t *)pb->buf_ptr) = ((nal_end - nal_start)>>0) & 0xff;
		pb->buf_ptr += 1;
		memcpy(pb->buf_ptr, nal_start, nal_end - nal_start);
		pb->buf_ptr += (nal_end - nal_start);
		//avio_wb32(pb, nal_end - nal_start);//nalu的有效数据，不包含开始标志0x00000001
        //avio_write(pb, nal_start, nal_end - nal_start);//写nalu的有效数据到
        size += 4 + nal_end - nal_start;
        nal_start = nal_end;
    }
    return size;
}


int ff_avc_parse_nal_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size)
{
    AVIOContext *pb = NULL;
	pb = (AVIOContext *)malloc(sizeof(AVIOContext));
	if(NULL == pb)
	{
		goto err_out;
	}
	pb->buffer_size = *size;
	pb->buffer = (unsigned char *)malloc(pb->buffer_size);
	if(NULL == pb->buffer)
	{
		goto err_out_free1;
	}
	pb->buf_ptr = pb->buffer;
	
    ff_avc_parse_nal_units(pb, buf_in, *size);

	*buf = pb->buffer;
	if(NULL != pb)
	{
		free(pb);
	}
	
    return 0;


err_out_free1:
	if(NULL != pb)
	{
		free(pb);
	}

err_out:
	return -1;

}


//cwm libavformat/avc.c
#endif


#if 1
//cwm libavformat/avio.c

#include <string.h>

#include "libavformat/avio.h"

void avio_w8(struct avio_buffer *s, uint8_t data)
{
	s->buf[s->pos] = data;
	s->pos ++;
}

void avio_wb32(struct avio_buffer *s, uint32_t val)
{
    avio_w8(s,           val >> 24 );
    avio_w8(s, (uint8_t)(val >> 16));
    avio_w8(s, (uint8_t)(val >> 8 ));
    avio_w8(s, (uint8_t) val       );
}

void avio_wb16(struct avio_buffer *s, uint16_t val)
{
    avio_w8(s, (int)val >> 8);
    avio_w8(s, (uint8_t)val);
}

void avio_write(struct avio_buffer *pab, const unsigned char *buf, int size)
{
	memcpy(pab->buf + pab->pos, buf, size);
	pab->pos += size;
}

//cwm libavformat/avio.c
#endif


#if 1
//cwm libavutil/mem.c

#define FF_MEMORY_POISON 0x2a /*cwm_add*/

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/dynarray.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"


static size_t max_alloc_size= INT_MAX;

void *av_malloc(size_t size)
{
    void *ptr = NULL;
    /* let's disallow possibly ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

    ptr = malloc(size);
    if (!ptr && !size)
    {
        size = 1;
        ptr = av_malloc(1);
    }

    return ptr;
}

void *av_realloc(void *ptr, size_t size)
{

    /* let's disallow possibly ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

    return realloc(ptr, size + !size);
}

void *av_realloc_f(void *ptr, size_t nelem, size_t elsize)
{
    size_t size;
    void *r;

    if (av_size_mult(elsize, nelem, &size)) {
        av_free(ptr);
        return NULL;
    }
    r = av_realloc(ptr, size);
    if (!r && size)
        av_free(ptr);
    return r;
}

int av_reallocp(void *ptr, size_t size)
{
    void *val;

    if (!size) {
        av_freep(ptr);
        return 0;
    }

    memcpy(&val, ptr, sizeof(val));
    val = av_realloc(val, size);

    if (!val) {
        av_freep(ptr);
        return AVERROR(ENOMEM);
    }

    memcpy(ptr, &val, sizeof(val));
    return 0;
}

void *av_realloc_array(void *ptr, size_t nmemb, size_t size)
{
    if (!size || nmemb >= INT_MAX / size)
        return NULL;
    return av_realloc(ptr, nmemb * size);
}

int av_reallocp_array(void *ptr, size_t nmemb, size_t size)
{
    void *val;

    memcpy(&val, ptr, sizeof(val));
    val = av_realloc_f(val, nmemb, size);
    memcpy(ptr, &val, sizeof(val));
    if (!val && nmemb && size)
        return AVERROR(ENOMEM);

    return 0;
}

void av_free(void *ptr)
{
    free(ptr);
}

#if 0	/* modify by liwei */
void av_freep(void *arg)
{
    void *val;

    memcpy(&val, arg, sizeof(val));	
    memcpy(arg, &(void *){ NULL }, sizeof(val));

    av_free(val);
}
#else
void av_freep(void *arg)
{
    void *val;
	void *tmp = {NULL};

    memcpy(&val, arg, sizeof(val));	
    memcpy(arg, &tmp, sizeof(val));

    av_free(val);
}
#endif

void *av_mallocz(size_t size)
{
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}


//cwm libavutil/mem.c
#endif

static void hvcc_update_ptl(HEVCDecoderConfigurationRecord *hvcc,
                            HVCCProfileTierLevel *ptl)
{
    /*
     * The value of general_profile_space in all the parameter sets must be
     * identical.
     */
    hvcc->general_profile_space = ptl->profile_space;

    /*
     * The level indication general_level_idc must indicate a level of
     * capability equal to or greater than the highest level indicated for the
     * highest tier in all the parameter sets.
     */
    if (hvcc->general_tier_flag < ptl->tier_flag)
        hvcc->general_level_idc = ptl->level_idc;
    else
        hvcc->general_level_idc = FFMAX(hvcc->general_level_idc, ptl->level_idc);

    /*
     * The tier indication general_tier_flag must indicate a tier equal to or
     * greater than the highest tier indicated in all the parameter sets.
     */
    hvcc->general_tier_flag = FFMAX(hvcc->general_tier_flag, ptl->tier_flag);

    /*
     * The profile indication general_profile_idc must indicate a profile to
     * which the stream associated with this configuration record conforms.
     *
     * If the sequence parameter sets are marked with different profiles, then
     * the stream may need examination to determine which profile, if any, the
     * entire stream conforms to. If the entire stream is not examined, or the
     * examination reveals that there is no profile to which the entire stream
     * conforms, then the entire stream must be split into two or more
     * sub-streams with separate configuration records in which these rules can
     * be met.
     *
     * Note: set the profile to the highest value for the sake of simplicity.
     */
    hvcc->general_profile_idc = FFMAX(hvcc->general_profile_idc, ptl->profile_idc);

    /*
     * Each bit in general_profile_compatibility_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_profile_compatibility_flags &= ptl->profile_compatibility_flags;

    /*
     * Each bit in general_constraint_indicator_flags may only be set if all
     * the parameter sets set that bit.
     */
    hvcc->general_constraint_indicator_flags &= ptl->constraint_indicator_flags;
}
//cwm 
static void hvcc_parse_ptl(GetBitContext *gb,
                           HEVCDecoderConfigurationRecord *hvcc,
                           unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    HVCCProfileTierLevel general_ptl;
    uint8_t sub_layer_profile_present_flag[MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[MAX_SUB_LAYERS];

    general_ptl.profile_space               = get_bits(gb, 2);
    general_ptl.tier_flag                   = get_bits1(gb);
    general_ptl.profile_idc                 = get_bits(gb, 5);
    general_ptl.profile_compatibility_flags = get_bits_long(gb, 32);
    general_ptl.constraint_indicator_flags  = get_bits64(gb, 48);
    general_ptl.level_idc                   = get_bits(gb, 8);
    hvcc_update_ptl(hvcc, &general_ptl);

    for (i = 0; i < max_sub_layers_minus1; i++) {//cwm false
        sub_layer_profile_present_flag[i] = get_bits1(gb);
        sub_layer_level_present_flag[i]   = get_bits1(gb);
    }

    if (max_sub_layers_minus1 > 0)//cwm false
        for (i = max_sub_layers_minus1; i < 8; i++)
            skip_bits(gb, 2); // reserved_zero_2bits[i]

    for (i = 0; i < max_sub_layers_minus1; i++) {//cwm false
        if (sub_layer_profile_present_flag[i]) {
            /*
             * sub_layer_profile_space[i]                     u(2)
             * sub_layer_tier_flag[i]                         u(1)
             * sub_layer_profile_idc[i]                       u(5)
             * sub_layer_profile_compatibility_flag[i][0..31] u(32)
             * sub_layer_progressive_source_flag[i]           u(1)
             * sub_layer_interlaced_source_flag[i]            u(1)
             * sub_layer_non_packed_constraint_flag[i]        u(1)
             * sub_layer_frame_only_constraint_flag[i]        u(1)
             * sub_layer_reserved_zero_44bits[i]              u(44)
             */
            skip_bits_long(gb, 32);
            skip_bits_long(gb, 32);
            skip_bits     (gb, 24);
        }

        if (sub_layer_level_present_flag[i])
            skip_bits(gb, 8);
    }
}

static void skip_sub_layer_hrd_parameters(GetBitContext *gb,
                                          unsigned int cpb_cnt_minus1,
                                          uint8_t sub_pic_hrd_params_present_flag)
{
    unsigned int i;

    for (i = 0; i <= cpb_cnt_minus1; i++) {
        get_ue_golomb_long(gb); // bit_rate_value_minus1
        get_ue_golomb_long(gb); // cpb_size_value_minus1

        if (sub_pic_hrd_params_present_flag) {
            get_ue_golomb_long(gb); // cpb_size_du_value_minus1
            get_ue_golomb_long(gb); // bit_rate_du_value_minus1
        }

        skip_bits1(gb); // cbr_flag
    }
}

static int skip_hrd_parameters(GetBitContext *gb, uint8_t cprms_present_flag,
                                unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    uint8_t sub_pic_hrd_params_present_flag = 0;
    uint8_t nal_hrd_parameters_present_flag = 0;
    uint8_t vcl_hrd_parameters_present_flag = 0;

    if (cprms_present_flag) {
        nal_hrd_parameters_present_flag = get_bits1(gb);
        vcl_hrd_parameters_present_flag = get_bits1(gb);

        if (nal_hrd_parameters_present_flag ||
            vcl_hrd_parameters_present_flag) {
            sub_pic_hrd_params_present_flag = get_bits1(gb);

            if (sub_pic_hrd_params_present_flag)
                /*
                 * tick_divisor_minus2                          u(8)
                 * du_cpb_removal_delay_increment_length_minus1 u(5)
                 * sub_pic_cpb_params_in_pic_timing_sei_flag    u(1)
                 * dpb_output_delay_du_length_minus1            u(5)
                 */
                skip_bits(gb, 19);

            /*
             * bit_rate_scale u(4)
             * cpb_size_scale u(4)
             */
            skip_bits(gb, 8);

            if (sub_pic_hrd_params_present_flag)
                skip_bits(gb, 4); // cpb_size_du_scale

            /*
             * initial_cpb_removal_delay_length_minus1 u(5)
             * au_cpb_removal_delay_length_minus1      u(5)
             * dpb_output_delay_length_minus1          u(5)
             */
            skip_bits(gb, 15);
        }
    }

    for (i = 0; i <= max_sub_layers_minus1; i++) {
        unsigned int cpb_cnt_minus1            = 0;
        uint8_t low_delay_hrd_flag             = 0;
        uint8_t fixed_pic_rate_within_cvs_flag = 0;
        uint8_t fixed_pic_rate_general_flag    = get_bits1(gb);

        if (!fixed_pic_rate_general_flag)
            fixed_pic_rate_within_cvs_flag = get_bits1(gb);

        if (fixed_pic_rate_within_cvs_flag)
            get_ue_golomb_long(gb); // elemental_duration_in_tc_minus1
        else
            low_delay_hrd_flag = get_bits1(gb);

        if (!low_delay_hrd_flag) {
            cpb_cnt_minus1 = get_ue_golomb_long(gb);
            if (cpb_cnt_minus1 > 31)
                return AVERROR_INVALIDDATA;
        }

        if (nal_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(gb, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);

        if (vcl_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(gb, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);
    }

    return 0;
}

static void skip_timing_info(GetBitContext *gb)
{
    skip_bits_long(gb, 32); // num_units_in_tick
    skip_bits_long(gb, 32); // time_scale

    if (get_bits1(gb))          // poc_proportional_to_timing_flag
        get_ue_golomb_long(gb); // num_ticks_poc_diff_one_minus1
}

static void hvcc_parse_vui(GetBitContext *gb,
                           HEVCDecoderConfigurationRecord *hvcc,
                           unsigned int max_sub_layers_minus1)
{
    unsigned int min_spatial_segmentation_idc;

    if (get_bits1(gb))              // aspect_ratio_info_present_flag
        if (get_bits(gb, 8) == 255) // aspect_ratio_idc
            skip_bits_long(gb, 32); // sar_width u(16), sar_height u(16)

    if (get_bits1(gb))  // overscan_info_present_flag
        skip_bits1(gb); // overscan_appropriate_flag

    if (get_bits1(gb)) {  // video_signal_type_present_flag
        skip_bits(gb, 4); // video_format u(3), video_full_range_flag u(1)

        if (get_bits1(gb)) // colour_description_present_flag
            /*
             * colour_primaries         u(8)
             * transfer_characteristics u(8)
             * matrix_coeffs            u(8)
             */
            skip_bits(gb, 24);
    }

    if (get_bits1(gb)) {        // chroma_loc_info_present_flag
        get_ue_golomb_long(gb); // chroma_sample_loc_type_top_field
        get_ue_golomb_long(gb); // chroma_sample_loc_type_bottom_field
    }

    /*
     * neutral_chroma_indication_flag u(1)
     * field_seq_flag                 u(1)
     * frame_field_info_present_flag  u(1)
     */
    skip_bits(gb, 3);

    if (get_bits1(gb)) {        // default_display_window_flag
        get_ue_golomb_long(gb); // def_disp_win_left_offset
        get_ue_golomb_long(gb); // def_disp_win_right_offset
        get_ue_golomb_long(gb); // def_disp_win_top_offset
        get_ue_golomb_long(gb); // def_disp_win_bottom_offset
    }

    if (get_bits1(gb)) { // vui_timing_info_present_flag
        skip_timing_info(gb);

        if (get_bits1(gb)) // vui_hrd_parameters_present_flag
            skip_hrd_parameters(gb, 1, max_sub_layers_minus1);
    }

    if (get_bits1(gb)) { // bitstream_restriction_flag
        /*
         * tiles_fixed_structure_flag              u(1)
         * motion_vectors_over_pic_boundaries_flag u(1)
         * restricted_ref_pic_lists_flag           u(1)
         */
        skip_bits(gb, 3);

        min_spatial_segmentation_idc = get_ue_golomb_long(gb);

        /*
         * unsigned int(12) min_spatial_segmentation_idc;
         *
         * The min_spatial_segmentation_idc indication must indicate a level of
         * spatial segmentation equal to or less than the lowest level of
         * spatial segmentation indicated in all the parameter sets.
         */
        hvcc->min_spatial_segmentation_idc = FFMIN(hvcc->min_spatial_segmentation_idc,
                                                   min_spatial_segmentation_idc);

        get_ue_golomb_long(gb); // max_bytes_per_pic_denom
        get_ue_golomb_long(gb); // max_bits_per_min_cu_denom
        get_ue_golomb_long(gb); // log2_max_mv_length_horizontal
        get_ue_golomb_long(gb); // log2_max_mv_length_vertical
    }
}

static void skip_sub_layer_ordering_info(GetBitContext *gb)
{
    get_ue_golomb_long(gb); // max_dec_pic_buffering_minus1
    get_ue_golomb_long(gb); // max_num_reorder_pics
    get_ue_golomb_long(gb); // max_latency_increase_plus1
}

static int hvcc_parse_vps(GetBitContext *gb,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    unsigned int vps_max_sub_layers_minus1;

    /*
     * vps_video_parameter_set_id u(4)
     * vps_reserved_three_2bits   u(2)
     * vps_max_layers_minus1      u(6)
     */
    skip_bits(gb, 12);

    vps_max_sub_layers_minus1 = get_bits(gb, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    hvcc->numTemporalLayers = FFMAX(hvcc->numTemporalLayers,
                                    vps_max_sub_layers_minus1 + 1);

    /*
     * vps_temporal_id_nesting_flag u(1)
     * vps_reserved_0xffff_16bits   u(16)
     */
    skip_bits(gb, 17);

    hvcc_parse_ptl(gb, hvcc, vps_max_sub_layers_minus1);

    /* nothing useful for hvcC past this point */
    return 0;
}

static void skip_scaling_list_data(GetBitContext *gb)
{
    int i, j, k, num_coeffs;

    for (i = 0; i < 4; i++)
        for (j = 0; j < (i == 3 ? 2 : 6); j++)
            if (!get_bits1(gb))         // scaling_list_pred_mode_flag[i][j]
                get_ue_golomb_long(gb); // scaling_list_pred_matrix_id_delta[i][j]
            else {
                num_coeffs = FFMIN(64, 1 << (4 + (i << 1)));

                if (i > 1)
                    get_se_golomb_long(gb); // scaling_list_dc_coef_minus8[i-2][j]

                for (k = 0; k < num_coeffs; k++)
                    get_se_golomb_long(gb); // scaling_list_delta_coef
            }
}

static int parse_rps(GetBitContext *gb, unsigned int rps_idx,
                     unsigned int num_rps,
                     unsigned int num_delta_pocs[MAX_SHORT_TERM_RPS_COUNT])
{
    unsigned int i;

    if (rps_idx && get_bits1(gb)) { // inter_ref_pic_set_prediction_flag
        /* this should only happen for slice headers, and this isn't one */
        if (rps_idx >= num_rps)
            return AVERROR_INVALIDDATA;

        skip_bits1        (gb); // delta_rps_sign
        get_ue_golomb_long(gb); // abs_delta_rps_minus1

        num_delta_pocs[rps_idx] = 0;

        /*
         * From libavcodec/hevc_ps.c:
         *
         * if (is_slice_header) {
         *    //foo
         * } else
         *     rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];
         *
         * where:
         * rps:             &sps->st_rps[rps_idx]
         * sps->st_rps:     &sps->st_rps[0]
         * is_slice_header: rps_idx == num_rps
         *
         * thus:
         * if (num_rps != rps_idx)
         *     rps_ridx = &sps->st_rps[rps_idx - 1];
         *
         * NumDeltaPocs[RefRpsIdx]: num_delta_pocs[rps_idx - 1]
         */
        for (i = 0; i <= num_delta_pocs[rps_idx - 1]; i++) {
            uint8_t use_delta_flag = 0;
            uint8_t used_by_curr_pic_flag = get_bits1(gb);
            if (!used_by_curr_pic_flag)
                use_delta_flag = get_bits1(gb);

            if (used_by_curr_pic_flag || use_delta_flag)
                num_delta_pocs[rps_idx]++;
        }
    } else {
        unsigned int num_negative_pics = get_ue_golomb_long(gb);
        unsigned int num_positive_pics = get_ue_golomb_long(gb);

        if (((int64_t)num_positive_pics + (int64_t)num_negative_pics) * 2 > get_bits_left(gb))
            return AVERROR_INVALIDDATA;

        num_delta_pocs[rps_idx] = num_negative_pics + num_positive_pics;

        for (i = 0; i < num_negative_pics; i++) {
            get_ue_golomb_long(gb); // delta_poc_s0_minus1[rps_idx]
            skip_bits1        (gb); // used_by_curr_pic_s0_flag[rps_idx]
        }

        for (i = 0; i < num_positive_pics; i++) {
            get_ue_golomb_long(gb); // delta_poc_s1_minus1[rps_idx]
            skip_bits1        (gb); // used_by_curr_pic_s1_flag[rps_idx]
        }
    }

    return 0;
}

static int hvcc_parse_sps(GetBitContext *gb,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    unsigned int i, sps_max_sub_layers_minus1, log2_max_pic_order_cnt_lsb_minus4;
    unsigned int num_short_term_ref_pic_sets, num_delta_pocs[MAX_SHORT_TERM_RPS_COUNT];

    skip_bits(gb, 4); // sps_video_parameter_set_id

    sps_max_sub_layers_minus1 = get_bits (gb, 3);

    /*
     * numTemporalLayers greater than 1 indicates that the stream to which this
     * configuration record applies is temporally scalable and the contained
     * number of temporal layers (also referred to as temporal sub-layer or
     * sub-layer in ISO/IEC 23008-2) is equal to numTemporalLayers. Value 1
     * indicates that the stream is not temporally scalable. Value 0 indicates
     * that it is unknown whether the stream is temporally scalable.
     */
    hvcc->numTemporalLayers = FFMAX(hvcc->numTemporalLayers,
                                    sps_max_sub_layers_minus1 + 1);

    hvcc->temporalIdNested = get_bits1(gb);

    hvcc_parse_ptl(gb, hvcc, sps_max_sub_layers_minus1);

    get_ue_golomb_long(gb); // sps_seq_parameter_set_id

    hvcc->chromaFormat = get_ue_golomb_long(gb);//cwm 1

    if (hvcc->chromaFormat == 3)
        skip_bits1(gb); // separate_colour_plane_flag

    get_ue_golomb_long(gb); // pic_width_in_luma_samples
    get_ue_golomb_long(gb); // pic_height_in_luma_samples

    if (get_bits1(gb)) {        // conformance_window_flag
        get_ue_golomb_long(gb); // conf_win_left_offset
        get_ue_golomb_long(gb); // conf_win_right_offset
        get_ue_golomb_long(gb); // conf_win_top_offset
        get_ue_golomb_long(gb); // conf_win_bottom_offset
    }

    hvcc->bitDepthLumaMinus8          = get_ue_golomb_long(gb);
    hvcc->bitDepthChromaMinus8        = get_ue_golomb_long(gb);
    log2_max_pic_order_cnt_lsb_minus4 = get_ue_golomb_long(gb);

    /* sps_sub_layer_ordering_info_present_flag */
    i = get_bits1(gb) ? 0 : sps_max_sub_layers_minus1;
    for (; i <= sps_max_sub_layers_minus1; i++)//cwm sps_max_sub_layers_minus1 = 0
        skip_sub_layer_ordering_info(gb);//cwm

    get_ue_golomb_long(gb); // log2_min_luma_coding_block_size_minus3
    get_ue_golomb_long(gb); // log2_diff_max_min_luma_coding_block_size
    get_ue_golomb_long(gb); // log2_min_transform_block_size_minus2
    get_ue_golomb_long(gb); // log2_diff_max_min_transform_block_size
    get_ue_golomb_long(gb); // max_transform_hierarchy_depth_inter
    get_ue_golomb_long(gb); // max_transform_hierarchy_depth_intra

    if (get_bits1(gb) && // scaling_list_enabled_flag
        get_bits1(gb))   // sps_scaling_list_data_present_flag //cwm false
        skip_scaling_list_data(gb);

    skip_bits1(gb); // amp_enabled_flag
    skip_bits1(gb); // sample_adaptive_offset_enabled_flag

    if (get_bits1(gb)) {           // pcm_enabled_flag
        skip_bits         (gb, 4); // pcm_sample_bit_depth_luma_minus1
        skip_bits         (gb, 4); // pcm_sample_bit_depth_chroma_minus1
        get_ue_golomb_long(gb);    // log2_min_pcm_luma_coding_block_size_minus3
        get_ue_golomb_long(gb);    // log2_diff_max_min_pcm_luma_coding_block_size
        skip_bits1        (gb);    // pcm_loop_filter_disabled_flag
    }

    num_short_term_ref_pic_sets = get_ue_golomb_long(gb);
    if (num_short_term_ref_pic_sets > MAX_SHORT_TERM_RPS_COUNT)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < num_short_term_ref_pic_sets; i++) {//cwm num_short_term_ref_pic_sets=0
        int ret = parse_rps(gb, i, num_short_term_ref_pic_sets, num_delta_pocs);
        if (ret < 0)
            return ret;
    }

    if (get_bits1(gb)) {                               // long_term_ref_pics_present_flag
        unsigned num_long_term_ref_pics_sps = get_ue_golomb_long(gb);
        if (num_long_term_ref_pics_sps > 31U)//cwm false
            return AVERROR_INVALIDDATA;
        for (i = 0; i < num_long_term_ref_pics_sps; i++) { // num_long_term_ref_pics_sps //cwm num_long_term_ref_pics_sps=0
            int len = FFMIN(log2_max_pic_order_cnt_lsb_minus4 + 4, 16);
            skip_bits (gb, len); // lt_ref_pic_poc_lsb_sps[i]
            skip_bits1(gb);      // used_by_curr_pic_lt_sps_flag[i]
        }
    }

    skip_bits1(gb); // sps_temporal_mvp_enabled_flag
    skip_bits1(gb); // strong_intra_smoothing_enabled_flag

    if (get_bits1(gb)) // vui_parameters_present_flag //cwm false
        hvcc_parse_vui(gb, hvcc, sps_max_sub_layers_minus1);

    /* nothing useful for hvcC past this point */
    return 0;
}

static int hvcc_parse_pps(GetBitContext *gb,
                          HEVCDecoderConfigurationRecord *hvcc)
{
    uint8_t tiles_enabled_flag, entropy_coding_sync_enabled_flag;

    get_ue_golomb_long(gb); // pps_pic_parameter_set_id
    get_ue_golomb_long(gb); // pps_seq_parameter_set_id

    /*
     * dependent_slice_segments_enabled_flag u(1)
     * output_flag_present_flag              u(1)
     * num_extra_slice_header_bits           u(3)
     * sign_data_hiding_enabled_flag         u(1)
     * cabac_init_present_flag               u(1)
     */
    skip_bits(gb, 7);

    get_ue_golomb_long(gb); // num_ref_idx_l0_default_active_minus1
    get_ue_golomb_long(gb); // num_ref_idx_l1_default_active_minus1
    get_se_golomb_long(gb); // init_qp_minus26

    /*
     * constrained_intra_pred_flag u(1)
     * transform_skip_enabled_flag u(1)
     */
    skip_bits(gb, 2);

    if (get_bits1(gb))          // cu_qp_delta_enabled_flag
        get_ue_golomb_long(gb); // diff_cu_qp_delta_depth

    get_se_golomb_long(gb); // pps_cb_qp_offset
    get_se_golomb_long(gb); // pps_cr_qp_offset

    /*
     * pps_slice_chroma_qp_offsets_present_flag u(1)
     * weighted_pred_flag               u(1)
     * weighted_bipred_flag             u(1)
     * transquant_bypass_enabled_flag   u(1)
     */
    skip_bits(gb, 4);

    tiles_enabled_flag               = get_bits1(gb);
    entropy_coding_sync_enabled_flag = get_bits1(gb);

    if (entropy_coding_sync_enabled_flag && tiles_enabled_flag)
        hvcc->parallelismType = 0; // mixed-type parallel decoding
    else if (entropy_coding_sync_enabled_flag)
        hvcc->parallelismType = 3; // wavefront-based parallel decoding
    else if (tiles_enabled_flag)
        hvcc->parallelismType = 2; // tile-based parallel decoding
    else
        hvcc->parallelismType = 1; // slice-based parallel decoding

    /* nothing useful for hvcC past this point */
    return 0;
}
//cwm 去掉有且只有连续两个0后面的一个字节，例如0x11 0x00 0x00 0x12 0x13,结果为0x11 0x00 0x00 0x13
static uint8_t *nal_unit_extract_rbsp(const uint8_t *src, uint32_t src_len,
                                      uint32_t *dst_len)
{
    uint8_t *dst;
    uint32_t i, len;

    dst = (uint8_t *)av_malloc(src_len + AV_INPUT_BUFFER_PADDING_SIZE);//cwm_add
    if (!dst)//cwm false
        return NULL;

    /* NAL unit header (2 bytes) */
    i = len = 0;
    while (i < 2 && i < src_len)
        dst[len++] = src[i++];

    while (i + 2 < src_len)
        if (!src[i] && !src[i + 1] && src[i + 2] == 3) {//cwm 寻找有且只有连续两个0的i值，跳过第3个字节
            dst[len++] = src[i++];//去掉有且只有连续两个0后面的一个字节，例如0x11 0x00 0x00 0x12 0x13,结果为0x11 0x00 0x00 0x13
            dst[len++] = src[i++];
            i++; // remove emulation_prevention_three_byte //跳过第3个字节
        } else
            dst[len++] = src[i++];

    while (i < src_len)
        dst[len++] = src[i++];//cwm 保留src末尾的最后两个字节

    *dst_len = len;
    return dst;
}


//cwm_add
static void nal_unit_parse_header(GetBitContext *gb, uint8_t *nal_type)
{
    skip_bits1(gb); // forbidden_zero_bit

    *nal_type = get_bits(gb, 6);

    /*
     * nuh_layer_id          u(6)
     * nuh_temporal_id_plus1 u(3)
     */
    skip_bits(gb, 9);
}

static int hvcc_array_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size,
                                   uint8_t nal_type, int ps_array_completeness,
                                   HEVCDecoderConfigurationRecord *hvcc)
{
    int ret;
    uint8_t index;
    uint16_t numNalus;
    HVCCNALUnitArray *array;

    for (index = 0; index < hvcc->numOfArrays; index++)//cwm false
        if (hvcc->array[index].NAL_unit_type == nal_type)
            break;

    if (index >= hvcc->numOfArrays) {//cwm true 0>=0
        uint8_t i;

        ret = av_reallocp_array(&hvcc->array, index + 1, sizeof(HVCCNALUnitArray));//cwm_add
        if (ret < 0)
            return ret;

        for (i = hvcc->numOfArrays; i <= index; i++)//cwm true
            memset(&hvcc->array[i], 0, sizeof(HVCCNALUnitArray));
        hvcc->numOfArrays = index + 1;
    }

    array    = &hvcc->array[index];
    numNalus = array->numNalus;

    ret = av_reallocp_array(&array->nalUnit, numNalus + 1, sizeof(uint8_t*));//cwm_add
    if (ret < 0)
        return ret;

    ret = av_reallocp_array(&array->nalUnitLength, numNalus + 1, sizeof(uint16_t));//cwm_add
    if (ret < 0)
        return ret;

    array->nalUnit      [numNalus] = nal_buf;//保存的是参数集的nal数据
    array->nalUnitLength[numNalus] = nal_size;//nal数据的长度
    array->NAL_unit_type           = nal_type;//nal数据的类型
    array->numNalus++;//nal的个数

    /*
     * When the sample entry name is the default and mandatory value of
     * array_completeness is 1 for arrays of all types of parameter sets, and 0
     * for all other arrays. When the sample entry name is the default
     * value of array_completeness is 0 for all arrays.
     */
    if (nal_type == NAL_VPS || nal_type == NAL_SPS || nal_type == NAL_PPS)
        array->array_completeness = ps_array_completeness;

    return 0;
}

static int hvcc_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size,
                             int ps_array_completeness,
                             HEVCDecoderConfigurationRecord *hvcc)//cwm ps_array_completeness=0
{
    int ret = 0;
    GetBitContext gbc;
    uint8_t nal_type;
    uint8_t *rbsp_buf;
    uint32_t rbsp_size;
	//rbsp_buf保存的是处理后的nal数据
    rbsp_buf = nal_unit_extract_rbsp(nal_buf, nal_size, &rbsp_size);//cwm 可移植
    if (!rbsp_buf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = init_get_bits8(&gbc, rbsp_buf, rbsp_size);//cwm 可移植
    if (ret < 0)
        goto end;

    nal_unit_parse_header(&gbc, &nal_type);//cwm 可移植

    /*
     * Note: only 'declarative' SEI messages are allowed in
     * hvcC. Perhaps the SEI playload type should be checked
     * and non-declarative SEI messages discarded?
     */
    switch (nal_type) {
    case NAL_VPS:
    case NAL_SPS:
    case NAL_PPS:
    case NAL_SEI_PREFIX:
    case NAL_SEI_SUFFIX:
        ret = hvcc_array_add_nal_unit(nal_buf, nal_size, nal_type,
                                      ps_array_completeness, hvcc);
        if (ret < 0)
            goto end;
        else if (nal_type == NAL_VPS)
            ret = hvcc_parse_vps(&gbc, hvcc);
        else if (nal_type == NAL_SPS)
            ret = hvcc_parse_sps(&gbc, hvcc);
        else if (nal_type == NAL_PPS)
            ret = hvcc_parse_pps(&gbc, hvcc);
        if (ret < 0)
        {
            goto end;
		}
        break;
    default:
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

end:
#if 0	/* modify by liwei */
    av_free(rbsp_buf);//cwm_add
#else
	if (rbsp_buf)
		av_free(rbsp_buf);//cwm_add
	// else
#endif
    return ret;
}

static void hvcc_init(HEVCDecoderConfigurationRecord *hvcc)
{
    memset(hvcc, 0, sizeof(HEVCDecoderConfigurationRecord));
    hvcc->configurationVersion = 1;
    hvcc->lengthSizeMinusOne   = 3; // 4 bytes

    /*
     * The following fields have all their valid bits set by default,
     * the ProfileTierLevel parsing code will unset them when needed.
     */
    hvcc->general_profile_compatibility_flags = 0xffffffff;
    hvcc->general_constraint_indicator_flags  = 0xffffffffffff;

    /*
     * Initialize this field with an invalid value which can be used to detect
     * whether we didn't see any VUI (in which case it should be reset to zero).
     */
    hvcc->min_spatial_segmentation_idc = MAX_SPATIAL_SEGMENTATION + 1;
	/* add by liwei */
	hvcc->init = 1;
}

static void hvcc_close(HEVCDecoderConfigurationRecord *hvcc)
{
    uint8_t i;

    for (i = 0; i < hvcc->numOfArrays; i++) {
        hvcc->array[i].numNalus = 0;
        av_freep(&hvcc->array[i].nalUnit);//cwm _add
        av_freep(&hvcc->array[i].nalUnitLength);//cwm _add
    }

    hvcc->numOfArrays = 0;
    av_freep(&hvcc->array);//cwm _add
	hvcc->init = 0;		/* add by liwei */
}

static int hvcc_write(AVIOContext *pb_tmp, HEVCDecoderConfigurationRecord *hvcc)
{
    uint8_t i;
    uint16_t j, vps_count = 0, sps_count = 0, pps_count = 0;

    /*
     * We only support writing HEVCDecoderConfigurationRecord version 1.
     */
    hvcc->configurationVersion = 1;

    /*
     * If min_spatial_segmentation_idc is invalid, reset to 0 (unspecified).
     */
    if (hvcc->min_spatial_segmentation_idc > MAX_SPATIAL_SEGMENTATION)
        hvcc->min_spatial_segmentation_idc = 0;

    /*
     * parallelismType indicates the type of parallelism that is used to meet
     * the restrictions imposed by min_spatial_segmentation_idc when the value
     * of min_spatial_segmentation_idc is greater than 0.
     */
    if (!hvcc->min_spatial_segmentation_idc)
        hvcc->parallelismType = 0;

    /*
     * It's unclear how to properly compute these fields, so
     * let's always set them to values meaning 'unspecified'.
     */
    hvcc->avgFrameRate      = 0;
    hvcc->constantFrameRate = 0;
    /*
     * We need at least one of each: VPS, SPS and PPS.
     */
    for (i = 0; i < hvcc->numOfArrays; i++)//cwm hvcc->array[i].numNalus = 3
        switch (hvcc->array[i].NAL_unit_type) {
        case NAL_VPS:
            vps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        case NAL_SPS:
            sps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        case NAL_PPS:
            pps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        default:
            break;
        }
    if (!vps_count || vps_count > MAX_VPS_COUNT ||
        !sps_count || sps_count > MAX_SPS_COUNT ||
        !pps_count || pps_count > MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;

    avio_buffer *pb = pb_tmp->pAvioBuf;
	//struct avio_buffer *pb = (struct avio_buffer *)malloc(sizeof(struct avio_buffer));
		
#if 1 //cwm 934
    /* unsigned int(8) configurationVersion = 1; */
    avio_w8(pb, hvcc->configurationVersion);//cwm , 1

    /*
     * unsigned int(2) general_profile_space;
     * unsigned int(1) general_tier_flag;
     * unsigned int(5) general_profile_idc;
     */
    avio_w8(pb, hvcc->general_profile_space << 6 |
                hvcc->general_tier_flag     << 5 |
                hvcc->general_profile_idc);//cwm ,1

    /* unsigned int(32) general_profile_compatibility_flags; */
    avio_wb32(pb, hvcc->general_profile_compatibility_flags);//cwm ,0x60000000

    /* unsigned int(48) general_constraint_indicator_flags; */
	
    avio_wb32(pb, hvcc->general_constraint_indicator_flags >> 16);//cwm ,0xb00000000000>>16=0xb0000000
    avio_wb16(pb, hvcc->general_constraint_indicator_flags);//cwm ,0xb00000000000&0xffff=0

    /* unsigned int(8) general_level_idc; */
    avio_w8(pb, hvcc->general_level_idc);//cwm, 0x5d

    /*
     * bit(4) reserved = 鈥?111鈥檅;
     * unsigned int(12) min_spatial_segmentation_idc;
     */
    avio_wb16(pb, hvcc->min_spatial_segmentation_idc | 0xf000);//cwm , 0 | 0xf000=0xf000

    /*
     * bit(6) reserved = 鈥?11111鈥檅;
     * unsigned int(2) parallelismType;
     */
    avio_w8(pb, hvcc->parallelismType | 0xfc);//cwm , 0|0xfc=0xfc=252

    /*
     * bit(6) reserved = 鈥?11111鈥檅;
     * unsigned int(2) chromaFormat;
     */
    avio_w8(pb, hvcc->chromaFormat | 0xfc);//cwm , 1 | 0xfc = 0xfd=253

    /*
     * bit(5) reserved = 鈥?1111鈥檅;
     * unsigned int(3) bitDepthLumaMinus8;
     */
    avio_w8(pb, hvcc->bitDepthLumaMinus8 | 0xf8);//cwm , 0 | 0xf8 = 0xf8 = 248

    /*
     * bit(5) reserved = 鈥?1111鈥檅;
     * unsigned int(3) bitDepthChromaMinus8;
     */
    avio_w8(pb, hvcc->bitDepthChromaMinus8 | 0xf8);//cwm , 0 | 0xf8 = 0xf8 = 248

    /* bit(16) avgFrameRate; */
    avio_wb16(pb, hvcc->avgFrameRate);//cwm , 0

    /*
     * bit(2) constantFrameRate;
     * bit(3) numTemporalLayers;
     * bit(1) temporalIdNested;
     * unsigned int(2) lengthSizeMinusOne;
     */
    avio_w8(pb, hvcc->constantFrameRate << 6 |
                hvcc->numTemporalLayers << 3 |
                hvcc->temporalIdNested  << 2 |
                hvcc->lengthSizeMinusOne);//cwm , 0<<6 | 1<<3 | 1<<2 | 0x3 = 0xf

    /* unsigned int(8) numOfArrays; */
    avio_w8(pb, hvcc->numOfArrays);//cwm , 0x3
	PrintInfo("hvcc->numOfArrays=%d", hvcc->numOfArrays);
    for (i = 0; i < hvcc->numOfArrays; i++) {
        /*
         * bit(1) array_completeness;
         * unsigned int(1) reserved = 0;
         * unsigned int(6) NAL_unit_type;
         */
        avio_w8(pb, hvcc->array[i].array_completeness << 7 |
                    (hvcc->array[i].NAL_unit_type & 0x3f));//cwm [0]=0x20, [1]=0x21, [2]=0x22

        /* unsigned int(16) numNalus; */
        avio_wb16(pb, hvcc->array[i].numNalus);//cwm , [0]=1, [1]=1,  [2]=1,

        for (j = 0; j < hvcc->array[i].numNalus; j++) {
            /* unsigned int(16) nalUnitLength; */
            avio_wb16(pb, hvcc->array[i].nalUnitLength[j]);//cwm , [0]=0x22, [1]=0x1d,  [2]=0x07

            /* bit(8*nalUnitLength) nalUnit; */
            avio_write(pb, hvcc->array[i].nalUnit[j],
                       hvcc->array[i].nalUnitLength[j]);
        }
    }
	
#endif //cwm 934

    return 0;
}

int ff_isom_write_hvcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)//ps_array_completeness = 0
{
    int ret = 0;
    uint8_t *buf, *end, *start = NULL;
    HEVCDecoderConfigurationRecord hvcc;//cwm 

    hvcc_init(&hvcc);
#if 0 //cwm1101
    if (size < 6) {//cwm false
        /* We can't write a valid hvcC from the provided data */
        ret = AVERROR_INVALIDDATA;
        goto end;
    } else if (*data == 1) {//cwm false
        /* Data is already hvcC-formatted */
        avio_write(pb, data, size);
        goto end;
    } else if (!(AV_RB24(data) == 1 || AV_RB32(data) == 1)) {//cwm false
        /* Not a valid Annex B start code prefix */
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
#endif //cwm1101
    ret = ff_avc_parse_nal_units_buf(data, &start, &size);//cwm 依次写vps、sps、pps的大小与有效数据到文件
    if (ret < 0)
        goto end;
	

    buf = start;//cwm start
    end = start + size;//cwm start + size

    while (end - buf > 4) {
        uint32_t len = FFMIN(AV_RB32(buf), end - buf - 4);//取最小值，len=32,是vps有效数据的长度
        uint8_t type = (buf[4] >> 1) & 0x3f;

        buf += 4;

        switch (type) {
        case NAL_VPS:
        case NAL_SPS:
        case NAL_PPS:
        case NAL_SEI_PREFIX:
        case NAL_SEI_SUFFIX:
            ret = hvcc_add_nal_unit(buf, len, ps_array_completeness, &hvcc);
            if (ret < 0)
                goto end;
            break;
        default:
            break;
        }

        buf += len;
    }

    ret = hvcc_write(pb, &hvcc);//写hvcc信息到文件

end:
    hvcc_close(&hvcc);
    av_free(start);//cwm_add
    return ret;
}


int mov_write_hvcc_tag(const uint8_t *data, int size, uint8_t **out_buf, uint32_t *out_size)
{
	AVIOContext avioct;
	static avio_buffer hvccAvioBuf;
	avioct.pAvioBuf = &hvccAvioBuf;
	
	avio_buffer *pab = &hvccAvioBuf;
	memset(pab->buf, 0, sizeof(pab->buf));
	pab->pos = 0;
	
    ff_isom_write_hvcc(&avioct, data, size, 0);//track->vos_len = 82    
    *out_buf = hvccAvioBuf.buf;
	*out_size = hvccAvioBuf.pos;
	
	return 0;
}

int mov_write_hev1_tag(uint16_t width, uint16_t height, uint8_t **out_addr, uint32_t *out_size)
{
	uint8_t buf[31] = {0};
	static avio_buffer hev1AvioBuf;
	
	avio_buffer *pab = &hev1AvioBuf;
	memset(pab->buf, 0, sizeof(pab->buf));
	pab->pos = 0;
	
    avio_wb32(pab, 0);
    avio_wb16(pab, 0);
    avio_wb16(pab, 1);
    avio_wb16(pab, 0);
    avio_wb16(pab, 0);
    avio_wb32(pab, 0);
    avio_wb32(pab, 0);
    avio_wb32(pab, 0);
    avio_wb16(pab, width);
    avio_wb16(pab, height);
    avio_wb32(pab, 0x00480000);
    avio_wb32(pab, 0x00480000);
    avio_wb32(pab, 0);
    avio_wb16(pab, 1);
    avio_w8(pab, 0);
	avio_write(pab, buf, sizeof(buf));
    avio_wb16(pab, 0x18);
    avio_wb16(pab, 0xffff);

	*out_addr = pab->buf;
	*out_size = pab->pos;
	return 0;
}


/************************************************* 
  Function: mov_hvcc_add_nal_unit
  Description: 对h265的nalu数据进行分析
  Input: nal_buf ，为h265nalu数据的开始地址，不包含开始标志0x00000001
  Input: nal_size，为h265 nalu数据的字节个数
  Input: hvcc，保存对h265 nalu分析的有效数据
  Output: 
  Return: 成功返回0，失败返回-1
  Author: 陈文敏
  Others: 20151208
*************************************************/
#if 0		/* modify by liwei */
int mov_hvcc_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size, HEVCDecoderConfigurationRecord *hvcc)//cwm ps_array_completeness=0
{
	int ret = 0;
	static int init_hvcc = 0;
	if (0 ==  init_hvcc)
	{		
    	hvcc_init( hvcc);
		init_hvcc = 1;
	}
    ret = hvcc_add_nal_unit(nal_buf, nal_size, 0, hvcc);
    if (ret < 0)
    {
		PrintInfo("hvcc_add_nal_unit failed!");
		return -1;
	}
	
	return 0;
}
#else
int mov_hvcc_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size, HEVCDecoderConfigurationRecord *hvcc)//cwm ps_array_completeness=0
{
	int ret = 0;
	
	if (0 ==  hvcc->init)
    	hvcc_init(hvcc);
    ret = hvcc_add_nal_unit(nal_buf, nal_size, 0, hvcc);
    if (ret < 0)
    {
		PrintInfo("hvcc_add_nal_unit failed!");
		return -1;
	}
	
	return 0;
}

#endif


/************************************************* 
  Function: mov_assm_hvcc_data
  Description:组装HEVCDecoderConfigurationRecord为HVCCData
  Input: hvcc，为分析nalu数据，得到的有效数据
  Output: hvcc_data，mp4的hvcC box所需的信息
  Return: 成功返回0，失败返回-1
  Author: 陈文敏
  Others: 20151208
*************************************************/

int mov_assm_hvcc_data(HEVCDecoderConfigurationRecord *hvcc, HVCCData *hvcc_data)
{
	if((NULL == hvcc) || (NULL == hvcc_data))
	{
		return -1;
	}

    uint8_t i;
    uint16_t j, vps_count = 0, sps_count = 0, pps_count = 0;
    (void)j;

    /*
     * We only support writing HEVCDecoderConfigurationRecord version 1.
     */
    hvcc->configurationVersion = 1;

    /*
     * If min_spatial_segmentation_idc is invalid, reset to 0 (unspecified).
     */
    if (hvcc->min_spatial_segmentation_idc > MAX_SPATIAL_SEGMENTATION)
        hvcc->min_spatial_segmentation_idc = 0;

    /*
     * parallelismType indicates the type of parallelism that is used to meet
     * the restrictions imposed by min_spatial_segmentation_idc when the value
     * of min_spatial_segmentation_idc is greater than 0.
     */
    if (!hvcc->min_spatial_segmentation_idc)
        hvcc->parallelismType = 0;

    /*
     * It's unclear how to properly compute these fields, so
     * let's always set them to values meaning 'unspecified'.
     */
    hvcc->avgFrameRate      = 0;
    hvcc->constantFrameRate = 0;
    /*
     * We need at least one of each: VPS, SPS and PPS.
     */
    for (i = 0; i < hvcc->numOfArrays; i++)//cwm hvcc->array[i].numNalus = 3
        switch (hvcc->array[i].NAL_unit_type) {
        case NAL_VPS:
            vps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        case NAL_SPS:
            sps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        case NAL_PPS:
            pps_count += hvcc->array[i].numNalus;//cwm 1
            break;
        default:
            break;
        }
    if (!vps_count || vps_count > MAX_VPS_COUNT ||
        !sps_count || sps_count > MAX_SPS_COUNT ||
        !pps_count || pps_count > MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;
 		
#if 1 //cwm 934
    /* unsigned int(8) configurationVersion = 1; */ 
	hvcc_data->m_hvcC_version_1B = hvcc->configurationVersion;
    /*
     * unsigned int(2) general_profile_space;
     * unsigned int(1) general_tier_flag;
     * unsigned int(5) general_profile_idc;
     */
    //cwm ,1
	hvcc_data->m_hvcC_profile_space_tier_flag_profile_idc_1B = hvcc->general_profile_space << 6 |
												                hvcc->general_tier_flag     << 5 |
												                hvcc->general_profile_idc;
    /* unsigned int(32) general_profile_compatibility_flags; */
    //cwm ,0x60000000
	hvcc_data->m_hvcC_profile_compatibility_flags_4B = hvcc->general_profile_compatibility_flags;
    /* unsigned int(48) general_constraint_indicator_flags; */
	
    //cwm ,0xb00000000000>>16=0xb0000000
    hvcc_data->m_hvcC_constraint_indicator_flags_4B = hvcc->general_constraint_indicator_flags >> 16;
    //cwm ,0xb00000000000&0xffff=0
	hvcc_data->m_hvcC_constraint_indicator_flags_2B = hvcc->general_constraint_indicator_flags;
    /* unsigned int(8) general_level_idc; */
    //cwm, 0x5d
	hvcc_data->m_hvcC_level_idc_1B = hvcc->general_level_idc;
    /*
     * bit(4) reserved = 鈥?111鈥檅;
     * unsigned int(12) min_spatial_segmentation_idc;
     */
    //cwm , 0 | 0xf000=0xf000
	hvcc_data->m_hvcC_min_spatial_segmentation_idc_2B = hvcc->min_spatial_segmentation_idc | 0xf000;
    /*
     * bit(6) reserved = 鈥?11111鈥檅;
     * unsigned int(2) parallelismType;
     */
    //cwm , 0|0xfc=0xfc=252
	hvcc_data->m_hvcC_parallelismType_1B = hvcc->parallelismType | 0xfc;
    /*
     * bit(6) reserved = 鈥?11111鈥檅;
     * unsigned int(2) chromaFormat;
     */
    //cwm , 1 | 0xfc = 0xfd=253
    hvcc_data->m_hvcC_chromaFormat_1B = hvcc->chromaFormat | 0xfc;

    /*
     * bit(5) reserved = 鈥?1111鈥檅;
     * unsigned int(3) bitDepthLumaMinus8;
     */
    //cwm , 0 | 0xf8 = 0xf8 = 248
	hvcc_data->m_hvcC_bitDepthLumaMinus8_1B = hvcc->bitDepthLumaMinus8 | 0xf8;
    /*
     * bit(5) reserved = 鈥?1111鈥檅;
     * unsigned int(3) bitDepthChromaMinus8;
     */
    //cwm , 0 | 0xf8 = 0xf8 = 248
	hvcc_data->m_hvcC_bitDepthChromaMinus8_1B = hvcc->bitDepthChromaMinus8 | 0xf8;
    /* bit(16) avgFrameRate; */
    //cwm , 0
	hvcc_data->m_hvcC_avgFrameRate_2B = hvcc->avgFrameRate;
    /*
     * bit(2) constantFrameRate;
     * bit(3) numTemporalLayers;
     * bit(1) temporalIdNested;
     * unsigned int(2) lengthSizeMinusOne;
     */
    //cwm , 0<<6 | 1<<3 | 1<<2 | 0x3 = 0xf
	hvcc_data->m_hvcC_constantFrameRate_numTemporalLayers_temporalIdNested_lengthSizeMinusOne_1B = hvcc->constantFrameRate << 6 |
																					                hvcc->numTemporalLayers << 3 |
																					                hvcc->temporalIdNested  << 2 |
																					                hvcc->lengthSizeMinusOne;
    /* unsigned int(8) numOfArrays; */
    //cwm , 0x3
    hvcc_data->m_hvcC_numOfArrays_1B = hvcc->numOfArrays;
	PrintInfo("hvcc->numOfArrays=%d", hvcc->numOfArrays); 
	
    hvcc_close(hvcc);
#endif //cwm 934

    return 0;
}

}




