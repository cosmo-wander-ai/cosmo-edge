/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#ifndef AVCODEC_HEVC_H
#define AVCODEC_HEVC_H

extern "C"{
#define MAX_DPB_SIZE 16 // A.4.1
#define MAX_REFS 16

#define MAX_NB_THREADS 16
#define SHIFT_CTB_WPP 2

/**
 * 7.4.2.1
 */
#define MAX_SUB_LAYERS 7
#define MAX_VPS_COUNT 16
#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256
#define MAX_SHORT_TERM_RPS_COUNT 64
#define MAX_CU_SIZE 128

//TODO: check if this is really the maximum
#define MAX_TRANSFORM_DEPTH 5

#define MAX_TB_SIZE 32
#define MAX_LOG2_CTB_SIZE 6
#define MAX_QP 51
#define DEFAULT_INTRA_TC_OFFSET 2

#define HEVC_CONTEXTS 199

#define MRG_MAX_NUM_CANDS     5

#define L0 0
#define L1 1

#define EPEL_EXTRA_BEFORE 1
#define EPEL_EXTRA_AFTER  2
#define EPEL_EXTRA        3
#define QPEL_EXTRA_BEFORE 3
#define QPEL_EXTRA_AFTER  4
#define QPEL_EXTRA        7

#define EDGE_EMU_BUFFER_STRIDE 80

/**
 * Value of the luma sample at position (x, y) in the 2D array tab.
 */
#define SAMPLE(tab, x, y) ((tab)[(y) * s->sps->width + (x)])
#define SAMPLE_CTB(tab, x, y) ((tab)[(y) * min_cb_width + (x)])

#define IS_IDR(s) ((s)->nal_unit_type == NAL_IDR_W_RADL || (s)->nal_unit_type == NAL_IDR_N_LP)
#define IS_BLA(s) ((s)->nal_unit_type == NAL_BLA_W_RADL || (s)->nal_unit_type == NAL_BLA_W_LP || \
                   (s)->nal_unit_type == NAL_BLA_N_LP)
#define IS_IRAP(s) ((s)->nal_unit_type >= 16 && (s)->nal_unit_type <= 23)

/**
 * Table 7-3: NAL unit type codes
 */
enum NALUnitType {
    NAL_TRAIL_N    = 0,//默认类型，无效
    NAL_TRAIL_R    = 1,
    NAL_TSA_N      = 2,
    NAL_TSA_R      = 3,
    NAL_STSA_N     = 4,
    NAL_STSA_R     = 5,
    NAL_RADL_N     = 6,
    NAL_RADL_R     = 7,
    NAL_RASL_N     = 8,
    NAL_RASL_R     = 9,
    NAL_BLA_W_LP   = 16,
    NAL_BLA_W_RADL = 17,
    NAL_BLA_N_LP   = 18,
    NAL_IDR_W_RADL = 19,
    NAL_IDR_N_LP   = 20,
    NAL_CRA_NUT    = 21,
    NAL_VPS        = 32,
    NAL_SPS        = 33,
    NAL_PPS        = 34,
    NAL_AUD        = 35,
    NAL_EOS_NUT    = 36,
    NAL_EOB_NUT    = 37,
    NAL_FD_NUT     = 38,
    NAL_SEI_PREFIX = 39,
    NAL_SEI_SUFFIX = 40,
};


#include <stdint.h>
#include "libavformat/avio.h"

typedef struct HVCCNALUnitArray {
    uint8_t  array_completeness;//固定值为0
    uint8_t  NAL_unit_type;
    uint16_t numNalus;
    uint16_t *nalUnitLength;
    uint8_t  **nalUnit;
} HVCCNALUnitArray;

typedef struct HEVCDecoderConfigurationRecord {
    uint8_t  configurationVersion;//cwm,固定值,1
    uint8_t  general_profile_space;//cwm 计算得到, 0(vps),0(sps)
    uint8_t  general_tier_flag;//cwm 计算得到, 0(vps),0(sps)
    uint8_t  general_profile_idc;//cwm 计算得到, 1(vps), 1(sps)
    uint32_t general_profile_compatibility_flags;//cwm 计算得到, 0x60000000(vps), 0x60000000(sps)
    uint64_t general_constraint_indicator_flags;//cwm 计算得到, 0xb00000000000(vps), 0xb00000000000(sps), general_constraint_indicator_flags>>16
    uint8_t  general_level_idc;//cwm 计算得到, 0x5d(vps), 0x5d(sps)
    uint16_t min_spatial_segmentation_idc;//cwm 计算得到,大于4096变为0,最后计算为0
    uint8_t  parallelismType;//cwm 计算得到, 1(pps),0，if(min_spatial_segmentation_idc == 0) parallelismType=0
    uint8_t  chromaFormat;//cwm 计算得到, 1(sps)
    uint8_t  bitDepthLumaMinus8;//cwm 计算得到, 0(sps)
    uint8_t  bitDepthChromaMinus8;//cwm 计算得到, 0(sps)
    uint16_t avgFrameRate;//cwm,固定值,0
    uint8_t  constantFrameRate;//cwm,固定值,0
    uint8_t  numTemporalLayers;//cwm 计算得到,1(vps),1(sps) max,
    uint8_t  temporalIdNested;//cwm 计算得到, 1(sps)
    uint8_t  lengthSizeMinusOne;//cwm 计算得到,3 ?
    uint8_t  numOfArrays;
	uint8_t  init;			/* add by liwei */
    HVCCNALUnitArray *array;
} HEVCDecoderConfigurationRecord;

typedef struct HVCCProfileTierLevel {
    uint8_t  profile_space;
    uint8_t  tier_flag;
    uint8_t  profile_idc;
    uint32_t profile_compatibility_flags;
    uint64_t constraint_indicator_flags;
    uint8_t  level_idc;
} HVCCProfileTierLevel;


typedef struct HVCCData{
	uint8_t 		m_hvcC_version_1B;//1，1字节，固定值，hvcc->configurationVersion，/* unsigned int(8) configurationVersion = 1; */
	uint8_t 		m_hvcC_profile_space_tier_flag_profile_idc_1B;//1，1字节，计算值，hvcc->general_profile_space << 6 | hvcc->general_tier_flag << 5 |               hvcc->general_profile_idc
	uint32_t 		m_hvcC_profile_compatibility_flags_4B;//0x60000000，4字节，计算值，hvcc->general_profile_compatibility_flags， /* unsigned int(32) general_profile_compatibility_flags; */
	uint32_t 		m_hvcC_constraint_indicator_flags_4B;//0xb0000000，4字节，计算值，hvcc->general_constraint_indicator_flags >> 16，/* unsigned int(48) general_constraint_indicator_flags; */
	uint16_t 		m_hvcC_constraint_indicator_flags_2B;//0,2字节，计算值，hvcc->general_constraint_indicator_flags，0xb00000000000&0xffff=0，/* unsigned int(48) general_constraint_indicator_flags; */
	uint8_t 		m_hvcC_level_idc_1B;//0x5d，1字节，计算值，hvcc->general_level_idc，/* unsigned int(8) general_level_idc; */
	uint16_t 		m_hvcC_min_spatial_segmentation_idc_2B;//0xf000，2字节，计算值，hvcc->min_spatial_segmentation_idc | 0xf000=0 | 0xf000=0xf000，
	uint8_t 		m_hvcC_parallelismType_1B;//0xfc，1字节，计算值，hvcc->parallelismType | 0xfc=0|0xfc=0xfc=252
	uint8_t 		m_hvcC_chromaFormat_1B;//0xfd，1字节，计算值，hvcc->chromaFormat | 0xfc=1 | 0xfc = 0xfd=253
	uint8_t 		m_hvcC_bitDepthLumaMinus8_1B;//0xf8，1字节，计算值， hvcc->bitDepthLumaMinus8 | 0xf8=0 | 0xf8 = 0xf8 = 248
	uint8_t 		m_hvcC_bitDepthChromaMinus8_1B;//0xf8，1字节，计算值，hvcc->bitDepthChromaMinus8 | 0xf8=0 | 0xf8 = 0xf8 = 248
	uint16_t 		m_hvcC_avgFrameRate_2B;//0,2字节，固定值，hvcc->avgFrameRate， /* bit(16) avgFrameRate; */
	uint8_t 		m_hvcC_constantFrameRate_numTemporalLayers_temporalIdNested_lengthSizeMinusOne_1B;//0xf，1字节，计算值，hvcc->constantFrameRate << 6 | hvcc->numTemporalLayers << 3 |               hvcc->temporalIdNested  << 2 | hvcc->lengthSizeMinusOne = 0<<6 | 1<<3 | 1<<2 | 0x3 = 0xf
	uint8_t 		m_hvcC_numOfArrays_1B;//0x3，1字节，计算值，hvcc->m_hvcC_numOfArrays_1B，/* unsigned int(8) m_hvcC_numOfArrays_1B; */	
}__attribute__((packed)) HVCCData;


/**
 * Writes HEVC extradata (parameter sets, declarative SEI NAL units) to the
 * provided AVIOContext.
 *
 * If the extradata is Annex B format, it gets converted to hvcC format before
 * writing.
 *
 * @param pb address of the AVIOContext where the hvcC shall be written
 * @param data address of the buffer holding the data needed to write the hvcC
 * @param size size (in bytes) of the data buffer
 * @param ps_array_completeness whether all parameter sets are in the hvcC (1)
 *        or there may be additional parameter sets in the bitstream (0)
 * @return >=0 in case of success, a negative value corresponding to an AVERROR
 *         code in case of failure
 */
int ff_isom_write_hvcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness);


int mov_write_hvcc_tag(const uint8_t *data, int size, uint8_t **out_buf, uint32_t *out_size);
int mov_write_hev1_tag(uint16_t width, uint16_t height, uint8_t **out_addr, uint32_t *out_size);

int mov_hvcc_add_nal_unit(uint8_t *nal_buf, uint32_t nal_size, HEVCDecoderConfigurationRecord *hvcc);
int mov_assm_hvcc_data(HEVCDecoderConfigurationRecord *hvcc, HVCCData *hvcc_data);

}

#endif /* AVCODEC_HEVC_H */

