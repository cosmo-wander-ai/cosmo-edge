/*****************************************************************************
 *
 *    Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
 *
 *    bmvid is licensed under the 2-Clause BSD License except for the
 *    third-party components.
 *
 *****************************************************************************/
/* This library provides a high-level interface for controlling the BitMain
 * Sophon VPU en/decoder.
 */

#ifndef BM_VIDEO_INTERFACE_H
#define BM_VIDEO_INTERFACE_H

#include "bmlib_runtime.h"


#define STREAM_BUF_SIZE                 0x700000
#define TRY_FLOCK_OPEN

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
#define ATTRIBUTE
#define DECL_EXPORT __declspec(dllexport)
#define DECL_IMPORT __declspec(dllimport)
#else
#define ATTRIBUTE __attribute__((deprecated))
#define DECL_EXPORT
#define DECL_IMPORT
#endif

#ifdef _WIN32
typedef unsigned long long u64;
#elif __linux__
typedef unsigned long u64;
#endif

#ifndef BOOL
typedef int     BOOL;
#endif

#ifndef TRUE
# define TRUE    1
#endif

#ifndef FALSE
# define FALSE   0
#endif

#ifdef _WIN32
typedef unsigned long long u64;
#else
typedef unsigned long u64;
#endif

#define MAX_FILE_PATH               256

typedef enum
{
    BMVPU_DEC_LOG_LEVEL_NONE =0,
    BMVPU_DEC_LOG_LEVEL_ERR,
    BMVPU_DEC_LOG_LEVEL_WARN,
    BMVPU_DEC_LOG_LEVEL_INFO,
    BMVPU_DEC_LOG_LEVEL_DEBUG,
    BMVPU_DEC_LOG_LEVEL_LOG,
    BMVPU_DEC_LOG_LEVEL_TRACE,
    BMVPU_DEC_LOG_LEVEL_MAX_LOG_LEVEL
} BmVpuDecLogLevel;
typedef enum
{
    BM_ERR_VDEC_SEQ_CHANGE = -28,
    BM_ERR_VDEC_INVALID_CHNID,
    BM_ERR_VDEC_ILLEGAL_PARAM,
    BM_ERR_VDEC_EXIST,
    BM_ERR_VDEC_UNEXIST,
    BM_ERR_VDEC_NULL_PTR,
    BM_ERR_VDEC_NOT_CONFIG,
    BM_ERR_VDEC_NOT_SUPPORT,
    BM_ERR_VDEC_NOT_PERM,
    BM_ERR_VDEC_INVALID_PIPEID,
    BM_ERR_VDEC_INVALID_GRPID,
    BM_ERR_VDEC_NOMEM,
    BM_ERR_VDEC_NOBUF,
    BM_ERR_VDEC_BUF_EMPTY,
    BM_ERR_VDEC_BUF_FULL,
    BM_ERR_VDEC_SYS_NOTREADY,
    BM_ERR_VDEC_BADADDR,
    BM_ERR_VDEC_BUSY,
    BM_ERR_VDEC_SIZE_NOT_ENOUGH,
    BM_ERR_VDEC_INVALID_VB,
    BM_ERR_VDEC_ERR_INIT,
    BM_ERR_VDEC_ERR_INVALID_RET,
    BM_ERR_VDEC_ERR_SEQ_OPER,
    BM_ERR_VDEC_ERR_VDEC_MUTEX,
    BM_ERR_VDEC_ERR_SEND_FAILED,
    BM_ERR_VDEC_ERR_GET_FAILED,
    BM_ERR_VDEC_BUTT,
    BM_ERR_VDEC_FAILURE,
    BM_ERR_VDEC_SUCCESS,
}BMVidDecRetStatus;


typedef enum{
    BMDEC_AVC       = 0,  //264
    BMDEC_HEVC      = 12,  //265
}BmVpuDecStreamFormat;

typedef enum  {
    BMDEC_FRAME_SKIP_MODE        = 0,     // disable skip mode,decode normally
    BMDEC_SKIP_NON_REF_NON_I    = 1,     // Skip non-reference non-intra frames
    BMDEC_SKIP_NON_I            = 2,     // Skip non-intra frames
}BmVpuDecSkipMode;

typedef struct {
    unsigned int  size;
    u64      phys_addr;
    u64      virt_addr;
} BmVpuDecDMABuffer;

typedef enum {
    BMDEC_OUTPUT_UNMAP,       // Original data
    BMDEC_OUTPUT_TILED = 100, // Output in tiled format(deprecated)
    BMDEC_OUTPUT_COMPRESSED,  // Output compressed data
} BmVpuDecOutputMapType;

/**
 * @brief Enum for different bitstream modes in BMDEC.
 *
 * This enum defines different modes for handling the bitstream in BMDEC.
 * - BMDEC_BS_MODE_INTERRUPT: Indicates the interrupt mode, which likely means the bitstream processing can be interrupted.
 * - BMDEC_BS_MODE_RESERVED: Represents a reserved mode, likely indicating a mode that is not currently used or defined.
 * - BMDEC_BS_MODE_PIC_END: Indicates the picture end mode, which likely means the end of a picture in the bitstream.
 */
typedef enum {
    BMDEC_BS_MODE_INTERRUPT = 0,  /**< Interrupt mode */
    BMDEC_BS_MODE_RESERVED,   /**< Reserved mode */
    BMDEC_BS_MODE_PIC_END   = 2,    /**< Picture end mode */
} BmVpuDecBitStreamMode;

typedef enum
{
    BM_VPU_DEC_PIX_FORMAT_YUV420P = 0,   /* planar 4:2:0 chroma_interleave is 0;*/
    BM_VPU_DEC_PIX_FORMAT_YUV422P = 1,   /* dec not support.*/
    BM_VPU_DEC_PIX_FORMAT_YUV444P = 3,   /* dec not support.*/
    BM_VPU_DEC_PIX_FORMAT_YUV400  = 4,   /* dec not support 8-bit greayscale */
    BM_VPU_DEC_PIX_FORMAT_NV12    = 5,   /* planar 4:2:0 chroma_interleave is 1, nv21 is 0;*/
    BM_VPU_DEC_PIX_FORMAT_NV21    = 6,   /* planar 4:2:0 chroma_interleave is 1, nv21 is 1*/
    BM_VPU_DEC_PIX_FORMAT_NV16    = 7,   /* dec not support.*/
    BM_VPU_DEC_PIX_FORMAT_NV24    = 8,   /* dec not support.*/
    BM_VPU_DEC_PIX_FORMAT_COMPRESSED = 9,
    BM_VPU_DEC_PIX_FORMAT_COMPRESSED_10BITS = 10,
} BmVpuDecPixFormat;

typedef struct {
    BmVpuDecStreamFormat        streamFormat;  //0:264
    BmVpuDecOutputMapType       wtlFormat;   //0:420 101: fbc
    BmVpuDecSkipMode            skip_mode;                  //2 only decode I frames.
    BmVpuDecBitStreamMode       bsMode;                     //!<<0, RING buffer interrupt. You don't know what's a frame.
    int                         enableCrop;                 //!<< option for saving yuv
    BmVpuDecPixFormat           pixel_format;
    int                         secondaryAXI;               //!<< enable secondary AXI

    int                         mp4class;
    int                         frameDelay;                 //!<< >0, output the display frame after frameDelay frames decoding.

    int                         pcie_board_id;
    int                         pcie_no_copyback;

    int                         enable_cache;
    int                         perf;
    int                         core_idx;
    int                         cmd_queue_depth;
    int                         decode_order;

    int                         picWidth;
    int                         picHeight;

    int                         timeout;
    int                         timeout_count;

    int                         extraFrameBufferNum;
    int                         min_framebuf_cnt;
    int                         framebuf_delay;
    int                         streamBufferSize;
    BmVpuDecDMABuffer*          bitstream_buffer;
    BmVpuDecDMABuffer*          frame_buffer;
    BmVpuDecDMABuffer*          Ytable_buffer;
    BmVpuDecDMABuffer*          Ctable_buffer;
    int                         reserved[13];
} BMVidDecParam;

typedef enum {
    BMDEC_UNCREATE,
    BMDEC_UNLOADING,
    BMDEC_UNINIT,
    BMDEC_INITING,
    BMDEC_WRONG_RESOLUTION,
    BMDEC_FRAMEBUFFER_NOTENOUGH,
    BMDEC_DECODING,
    BMDEC_FRAME_BUF_FULL,
    BMDEC_ENDOF,
    BMDEC_STOP,
    BMDEC_HUNG,
    BMDEC_CLOSE,
    BMDEC_CLOSED,
} BMDecStatus;

typedef enum{
    BMDEC_FLUSH_FAIL = -1,
    BMDEC_FLUSH_SUCCESS,
    BMDEC_FLUSH_BUF_FULL,
}BMDecFlushStatus;

typedef struct BMVidStream {
    unsigned char* buf;
    unsigned int length;
    unsigned char* header_buf;
    unsigned int header_size;
    unsigned char* extradata;
    unsigned int extradata_size;
    unsigned char end_of_stream;
    u64 pts;
    u64 dts;
} BMVidStream;

typedef struct {
    unsigned int left;    /**< A horizontal pixel offset of top-left corner of rectangle from (0, 0) */
    unsigned int top;     /**< A vertical pixel offset of top-left corner of rectangle from (0, 0) */
    unsigned int right;   /**< A horizontal pixel offset of bottom-right corner of rectangle from (0, 0) */
    unsigned int bottom;  /**< A vertical pixel offset of bottom-right corner of rectangle from (0, 0) */
} CropRect;


typedef struct BMVidStreamInfo {

/**
@verbatim
Horizontal picture size in pixel

This width value is used while allocating decoder frame buffers. In some
cases, this returned value, the display picture width declared on stream header,
should be aligned to a specific value depending on product and video standard before allocating frame buffers.
@endverbatim
*/
    int           picWidth;
/**
@verbatim
Vertical picture size in pixel

This height value is used while allocating decoder frame buffers.
In some cases, this returned value, the display picture height declared on stream header,
should be aligned to a specific value depending on product and video standard before allocating frame buffers.
@endverbatim
*/
    int           picHeight;

/**
@verbatim
The numerator part of frame rate fraction

NOTE: The meaning of this flag can vary by codec standards.
For details about this,
please refer to 'Appendix: FRAME RATE NUMERATORS in programmer\'s guide'.
@endverbatim
*/
    int           fRateNumerator;
/**
@verbatim
The denominator part of frame rate fraction

NOTE: The meaning of this flag can vary by codec standards.
For details about this,
please refer to 'Appendix: FRAME RATE DENOMINATORS in programmer\'s guide'.
@endverbatim
*/
    int           fRateDenominator;
/**
@verbatim
Picture cropping rectangle information (H.264/H.265/AVS decoder only)

This structure specifies the cropping rectangle information.
The size and position of cropping window in full frame buffer is presented
by using this structure.
@endverbatim
*/
    CropRect      picCropRect;
    int           mp4DataPartitionEnable; /**< data_partitioned syntax value in MPEG4 VOL header */
    int           mp4ReversibleVlcEnable; /**< reversible_vlc syntax value in MPEG4 VOL header */
/**
@verbatim
@* 0 : not h.263 stream
@* 1 : h.263 stream(mpeg4 short video header)
@endverbatim
*/
    int           mp4ShortVideoHeader;
/**
@verbatim
@* 0 : Annex J disabled
@* 1 : Annex J (optional deblocking filter mode) enabled
@endverbatim
*/
    int           h263AnnexJEnable;
    int           minFrameBufferCount;    /**< This is the minimum number of frame buffers required for decoding. Applications must allocate at least as many as this number of frame buffers and register the number of buffers to VPU using VPU_DecRegisterFrameBuffer() before decoding pictures. */
    int           frameBufDelay;          /**< This is the maximum display frame buffer delay for buffering decoded picture reorder. VPU may delay decoded picture display for display reordering when H.264/H.265, pic_order_cnt_type 0 or 1 case and for B-frame handling in VC1 decoder. */
    int           normalSliceSize;        /**< This is the recommended size of buffer used to save slice in normal case. This value is determined by quarter of the memory size for one raw YUV image in KB unit. This is only for H.264. */
    int           worstSliceSize;         /**< This is the recommended size of buffer used to save slice in worst case. This value is determined by half of the memory size for one raw YUV image in KB unit. This is only for H.264. */

    // Report Information
    int           maxSubLayers;           /**< Number of sub-layer for H.265/HEVC */
/**
@verbatim
@* H.265/H.264 : profile_idc
@* VC1
@** 0 : Simple profile
@** 1 : Main profile
@** 2 : Advanced profile
@* MPEG2
@** 3\'b101 : Simple
@** 3\'b100 : Main
@** 3\'b011 : SNR Scalable
@** 3\'b10 : Spatially Scalable
@** 3\'b001 : High
@* MPEG4
@** 8\'b00000000 : SP
@** 8\'b00001111 : ASP
@* Real Video
@** 8 (version 8)
@** 9 (version 9)
@** 10 (version 10)
@* AVS
@** 8\'b0010 0000 : Jizhun profile
@** 8\'b0100 1000 : Guangdian profile
@* VP8 : 0 - 3
@endverbatim
*/
    int           profile;
/**
@verbatim
@* H.265/H.264 : level_idc
@* VC1 : level
@* MPEG2 :
@** 4\'b1010 : Low
@** 4\'b1000 : Main
@** 4\'b0110 : High 1440,
@** 4\'b0100 : High
@* MPEG4 :
@** SP
@*** 4\'b1000 : L0
@*** 4\'b0001 : L1
@*** 4\'b0010 : L2
@*** 4\'b0011 : L3
@** ASP
@*** 4\'b0000 : L0
@*** 4\'b0001 : L1
@*** 4\'b0010 : L2
@*** 4\'b0011 : L3
@*** 4\'b0100 : L4
@*** 4\'b0101 : L5
@* Real Video : N/A (real video does not have any level info).
@* AVS :
@** 4\'b0000 : L2.0
@** 4\'b0001 : L4.0
@** 4\'b0010 : L4.2
@** 4\'b0011 : L6.0
@** 4\'b0100 : L6.2
@* VC1 : level in struct B
@endverbatim
*/
    int           level;
/**
@verbatim
A tier indicator

@* 0 : Main
@* 1 : High
@endverbatim
*/
    int           tier;
    int           interlace;              /**< When this value is 1, decoded stream may be decoded into progressive or interlace frame. Otherwise, decoded stream is progressive frame. */
    int           constraint_set_flag[4]; /**< constraint_set0_flag ~ constraint_set3_flag in H.264/AVC SPS */
    int           direct8x8Flag;          /**< direct_8x8_inference_flag in H.264/AVC SPS */
    int           vc1Psf;                 /**< Progressive Segmented Frame(PSF) in VC1 sequence layer */
    int           isExtSAR;
/**
@verbatim
This is one of the SPS syntax elements in H.264.

@* 0 : max_num_ref_frames is 0.
@* 1 : max_num_ref_frames is not 0.
@endverbatim
*/
    int           maxNumRefFrmFlag;
    int           maxNumRefFrm;
/**
@verbatim
@* H.264/AVC : When avcIsExtSAR is 0, this indicates aspect_ratio_idc[7:0]. When avcIsExtSAR is 1, this indicates sar_width[31:16] and sar_height[15:0].
If aspect_ratio_info_present_flag = 0, the register returns -1 (0xffffffff).
@* VC1 : this reports ASPECT_HORIZ_SIZE[15:8] and ASPECT_VERT_SIZE[7:0].
@* MPEG2 : this value is index of Table 6-3 in ISO/IEC 13818-2.
@* MPEG4/H.263 : this value is index of Table 6-12 in ISO/IEC 14496-2.
@* RV : aspect_ratio_info
@* AVS : this value is the aspect_ratio_info[3:0] which is used as index of Table 7-5 in AVS Part2
@endverbatim
*/
    int           aspectRateInfo;
    int           bitRate;        /**< The bitrate value written in bitstream syntax. If there is no bitRate, this reports -1. */
//    ThoScaleInfo    thoScaleInfo;   /**< This is the Theora picture size information. Refer to <<vpuapi_h_ThoScaleInfo>>. */
//    Vp8ScaleInfo    vp8ScaleInfo;   /**< This is VP8 upsampling information. Refer to <<vpuapi_h_Vp8ScaleInfo>>. */
    int           mp2LowDelay;    /**< This is low_delay syntax of sequence extension in MPEG2 specification. */
    int           mp2DispVerSize; /**< This is display_vertical_size syntax of sequence display extension in MPEG2 specification. */
    int           mp2DispHorSize; /**< This is display_horizontal_size syntax of sequence display extension in MPEG2 specification. */
    unsigned int          userDataHeader; /**< Refer to userDataHeader in <<vpuapi_h_DecOutputExtData>>. */
    int           userDataNum;    /**< Refer to userDataNum in <<vpuapi_h_DecOutputExtData>>. */
    int           userDataSize;   /**< Refer to userDataSize in <<vpuapi_h_DecOutputExtData>>. */
    int           userDataBufFull;/**< Refer to userDataBufFull in <<vpuapi_h_DecOutputExtData>>. */
    //VUI information
    int           chromaFormatIDC;/**< A chroma format indicator */
    int           lumaBitdepth;   /**< A bit-depth of luma sample */
    int           chromaBitdepth; /**< A bit-depth of chroma sample */
/**
@verbatim
This is an error reason of sequence header decoding.
For detailed meaning of returned value,
please refer to the 'Appendix: ERROR DEFINITION in programmer\'s guide'.
@endverbatim
*/
    int           seqInitErrReason;
    int           warnInfo;
//    AvcVuiInfo      avcVuiInfo;    /**< This is H.264/AVC VUI information. Refer to <<vpuapi_h_AvcVuiInfo>>. */
//    MP2BarDataInfo  mp2BardataInfo;/**< This is bar information in MPEG2 user data. For details about this, please see the document 'ATSC Digital Television Standard: Part 4:2009'. */
    unsigned int          sequenceNo;    /**< This is the number of sequence information. This variable is increased by 1 when VPU detects change of sequence. */
} BMVidStreamInfo;

/**
 * @brief Enum for defining the type of video frames in BmVpuDec.
 *
 * This enum defines the types of video frames, including progressive and interlaced frames.
 * - PROGRESSIVE_FRAME: Represents progressive scan frames, where each frame is displayed in a single pass without interlacing.
 * - INTERLACED_FRAME: Represents interlaced frames, where each frame consists of two interleaved fields displayed sequentially for smoother motion.
 */
typedef enum {
    PROGRESSIVE_FRAME = 0,  /**< Progressive scan frame */
    INTERLACED_FRAME =  1    /**< Interlaced frame */
} BmVpuDecLaceFrame;


/**
 * @brief Enum for specifying the format of frame buffers in BmVpuDec.
 *
 * This enum defines different formats for frame buffers in BmVpuDec.
 * - BMDEC_FORMAT_UNCOMPRESSED: Represents uncompressed frame buffer format.
 * - BMDEC_FORMAT_COMPRESSED: Represents compressed frame buffer format.
 * - BMDEC_FORMAT_COMPRESSED_10BITS: Represents 10-bit compressed frame buffer format.
 */

typedef enum {
    BMDEC_PIC_TYPE_I       = 0, /**< I picture */
    BMDEC_PIC_TYPE_P       = 1, /**< P picture */
    BMDEC_PIC_TYPE_B       = 2, /**< B picture (except VC1) */
    BMDEC_PIC_TYPE_D       = 3, /**< D picture in MPEG2 that is only composed of DC coefficients (MPEG2 only) */
    BMDEC_PIC_TYPE_S       = 4, /**< S picture in MPEG4 that is an acronym of Sprite and used for GMC (MPEG4 only)*/
    BMDEC_PIC_TYPE_IDR     = 5, /**< H.264/H.265 IDR picture */
    BMDEC_PIC_TYPE_AVS2_G  = 6, /**< G picture in AVS2 */
}BmVpuDecPicType;

#ifndef BMVIDFRAME
#define BMVIDFRAME
typedef struct BMVidFrame {
    BmVpuDecPicType picType;
    unsigned char* buf[8]; /**< 0: Y virt addr, 1: Cb virt addr: 2, Cr virt addr. 4: Y phy addr, 5: Cb phy addr, 6: Cr phy addr */
    int stride[8];
    unsigned int width;
    unsigned int height;
    BmVpuDecLaceFrame interlacedFrame;
    int lumaBitDepth;   /**< Bit depth for luma component */
    int chromaBitDepth; /**< Bit depth for chroma component  */
    BmVpuDecPixFormat pixel_format;

/**
@verbatim
It specifies endianess of frame buffer.

@* 0 : little endian format
@* 1 : big endian format
@* 2 : 32 bit little endian format
@* 3 : 32 bit big endian format
@* 16 ~ 31 : 128 bit endian format

NOTE: For setting specific values of 128 bit endiness, please refer to the 'WAVE Datasheet'.
@endverbatim
*/
    int endian;

    int sequenceNo;  /**< This variable increases by 1 whenever sequence changes. If it happens, HOST should call VPU_DecFrameBufferFlush() to get the decoded result that remains in the buffer in the form of DecOutputInfo array. HOST can recognize with this variable whether this frame is in the current sequence or in the previous sequence when it is displayed. (WAVE only) */

    int frameIdx;
    u64 pts;
    u64 dts;
    int colorPrimaries;
    int colorTransferCharacteristic;
    int colorSpace;
    int colorRange;
    int chromaLocation;
    int size; /**< Framebuffer size */
    unsigned int coded_width;
    unsigned int coded_height;
} BMVidFrame;
#endif
typedef void* BMVidCodHandle;

DECL_EXPORT void bmvpu_dec_set_logging_threshold(BmVpuDecLogLevel log_level);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_create(BMVidCodHandle* pVidCodHandle, BMVidDecParam decParam);
#ifdef BM_PCIE_MODE
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_read_memory(BMVidCodHandle vidCodHandle, u64 src_addr, u64 dst_addr, int size);
#endif
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_get_caps(BMVidCodHandle vidCodHandle, BMVidStreamInfo* streamInfo);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_decode(BMVidCodHandle vidCodHandle, BMVidStream vidStream);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_get_output(BMVidCodHandle vidCodHandle, BMVidFrame *bmFrame);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_clear_output(BMVidCodHandle vidCodHandle, BMVidFrame* frame);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_flush(BMVidCodHandle vidCodHandle); //in the endof of the file, flush and then close the decoder.

DECL_EXPORT BMVidDecRetStatus bmvpu_dec_delete(BMVidCodHandle vidCodHandle);
DECL_EXPORT BMDecStatus bmvpu_dec_get_status(BMVidCodHandle vidCodHandle);
DECL_EXPORT int bmvpu_dec_get_stream_buffer_empty_size(BMVidCodHandle vidCodHandle);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_get_all_frame_in_buffer(BMVidCodHandle vidCodHandle);
DECL_EXPORT int bmvpu_dec_get_all_empty_input_buf_cnt(BMVidCodHandle vidCodHandle);
DECL_EXPORT int bmvpu_dec_get_pkt_in_buf_cnt(BMVidCodHandle vidCodHandle);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_reset(int devIdx, int coreIdx);
DECL_EXPORT int bmvpu_dec_get_core_idx(BMVidCodHandle handle);
DECL_EXPORT BMVidDecRetStatus bmvpu_dec_dump_stream(BMVidCodHandle vidCodHandle, unsigned char *p_stream, int size);
DECL_EXPORT int bmvpu_dec_get_inst_idx(BMVidCodHandle vidCodHandle);
DECL_EXPORT int bmvpu_dec_get_device_fd(BMVidCodHandle vidCodHandle);

#endif
