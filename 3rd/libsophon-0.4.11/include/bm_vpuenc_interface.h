/* bmvpuapi API library for the BitMain Sophon SoC
 *
 * Copyright (C) 2018 Solan Shang
 * Copyright (C) 2015 Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#ifndef __BMVPUAPI_ENC_H__
#define __BMVPUAPI_ENC_H__

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
#define ATTRIBUTE
#define DECL_EXPORT __declspec(dllexport)
#define DECL_IMPORT __declspec(dllimport)
#else
#define ATTRIBUTE __attribute__((deprecated))
#define DECL_EXPORT
#define DECL_IMPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif


#define BMVPUAPI_VERSION "1.0.0"


#ifndef u64
#ifdef _WIN32
typedef unsigned long long u64;
#elif __linux__
typedef unsigned long u64;
#endif
#endif

#ifndef u32
#ifdef _WIN32
typedef unsigned int u32;
#endif
#endif



/**
 * How to use the encoder:
 *
 * Global initialization / shutdown is done by calling bmvpu_enc_load() and
 * bmvpu_enc_unload() respectively. These functions contain a reference counter,
 * so bmvpu_enc_unload() must be called as many times as bmvpu_enc_load() was,
 * or else it will not unload. Do not try to create a encoder before calling
 * bmvpu_enc_load(), as this function loads the VPU firmware. Likewise, the
 * bmvpu_enc_unload() function unloads the firmware. This firmware (un)loading
 * affects the entire process, not just the current thread.
 *
 * Typically, loading/unloading is done in two ways:
 * (1) bmvpu_enc_load() gets called in the startup phase of the process, and
 *     bmvpu_enc_unload() in the shutdown phase.
 * (2) bmvpu_enc_load() gets called every time before a encoder is to be created,
 *     and bmvpu_enc_unload() every time after a encoder was shut down.
 *
 * How to create, use, and shutdown an encoder:
 *  1. Call bmvpu_enc_get_bitstream_buffer_info(), and allocate a DMA buffer
 *     with the given size and alignment. This is the minimum required size.
 *     The buffer can be larger, but must not be smaller than the given size.
 *  2. Fill an instance of BmVpuEncOpenParams with the values specific to the
 *     input data. It is recommended to set default values by calling
 *     bmvpu_enc_set_default_open_params() and afterwards set any explicit valus.
 *  3. Call bmvpu_enc_open(), passing in a pointer to the filled BmVpuEncOpenParams
 *     instance, and the DMA buffer of the bitstream which was allocated in step 1.
 *  4. (Optional) allocate an array of at least as many DMA buffers as specified in
 *     min_num_src_fb for the input frames. If the incoming data is already stored in DMA buffers,
 *     this step can be omitted, since the encoder can then read the data directly.
 *  5. Create an instance of BmVpuRawFrame, set its values to zero.
 *  6. Create an instance of BmVpuEncodedFrame. Set its values to zero.
 *  7. Set the framebuffer pointer of the BmVpuRawFrame's instance from step 6 to refer to the
 *     input DMA buffer (either the one allocated in step 5, or the one containing the input data if
 *     it already comes in DMA memory).
 *  8. Fill an instance of BmVpuEncParams with valid values. It is recommended to first set its
 *     values to zero by using memset(). It is essential to make sure the acquire_output_buffer() and
 *     finish_output_buffer() function pointers are set, as these are used for acquiring buffers
 *     to write encoded output data into.
 *  9. (Optional) If step 5 was performed, and therefore input data does *not* come in DMA memory,
 *     copy the pixels from the raw input frames into the DMA buffer allocated in step 5. Otherwise,
 *     if the raw input frames are already stored in DMA memory, this step can be omitted.
 * 10. Call bmvpu_enc_encode(). Pass the raw frame, the encoded frame, and the encoding param
 *     structures from steps 6, 7, and 9 to it.
 *     This function will encode data, and acquire an output buffer to write the encoded data into
 *     by using the acquire_output_buffer() function pointer set in step 9. Once it is done
 *     encoding, it will call the finish_output_buffer() function from step 9. Any handle created
 *     by acquire_output_buffer() will be copied over to the encoded data frame structure. When
 *     bmvpu_enc_encode() exits, this handle can then be used to further process the output data.
 *     It is guaranteed that once acquire_output_buffer() was called, finish_output_buffer() will
 *     be called, even if an error occurred.
 *     The BM_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE output code bit will always be set
 *     unless the function returned a code other than BM_VPU_ENC_RETURN_CODE_OK.
 *     If the BM_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER bit is set, then header data has been
 *     written in the output memory block allocated in step 7. It is placed right before the
 *     actual encoded frame data. bmvpu_enc_encode() will pass over the combined size of the header
 *     and the encoded frame data to acquire_output_buffer() in this case, ensuring that the output
 *     buffers are big enough.
 * 11. Repeat steps 8 to 11 until there are no more frames to encode or an error occurs.
 * 12. After encoding is finished, close the encoder with bmvpu_enc_close().
 * 13. Deallocate framebuffer memory blocks, the input DMA buffer block, the output memory block,
 *     and the bitstream buffer memory block.
 *
 * The VPU's encoders only support the BM_VPU_ENC_PIX_FORMAT_YUV420P format.
 */

/**************************************************/
/******* LOG STRUCTURES AND FUNCTIONS *******/
/**************************************************/
/* Log levels. */
typedef enum
{
    BMVPU_ENC_LOG_LEVEL_ERROR   = 0,
    BMVPU_ENC_LOG_LEVEL_WARNING = 1,
    BMVPU_ENC_LOG_LEVEL_INFO    = 2,
    BMVPU_ENC_LOG_LEVEL_DEBUG   = 3, /* only useful for developers */
    BMVPU_ENC_LOG_LEVEL_LOG     = 4, /* only useful for developers */
    BMVPU_ENC_LOG_LEVEL_TRACE   = 5  /* only useful for developers */
} BmVpuEncLogLevel;

/* Function pointer type for logging functions.
 *
 * This function is invoked by BM_VPU_LOG() macro calls. This macro also passes the name
 * of the source file, the line in that file, and the function name where the logging occurs
 * to the logging function (over the file, line, and fn arguments, respectively).
 * Together with the log level, custom logging functions can output this metadata, or use
 * it for log filtering etc.*/
typedef void (*BmVpuEncLoggingFunc)(BmVpuEncLogLevel level, char const *file, int const line,
                                 char const *fn, const char *format, ...);

/* Defines the threshold for logging. Logs with lower priority are discarded.
 * By default, the threshold is set to BMVPU_ENC_LOG_LEVEL_INFO. */
DECL_EXPORT void bmvpu_enc_set_logging_threshold(BmVpuEncLogLevel threshold);

/* Defines a custom logging function.
 * If logging_fn is NULL, logging is disabled. This is the default value. */
DECL_EXPORT void bmvpu_enc_set_logging_function(BmVpuEncLoggingFunc logging_fn);

/* Get the threshold for logging. */
DECL_EXPORT BmVpuEncLogLevel bmvpu_enc_get_logging_threshold(void);




/**************************************************/
/******* ALLOCATOR STRUCTURES AND FUNCTIONS *******/
/**************************************************/

/* Typedef for physical addresses */
#ifdef __linux__
typedef unsigned long bmvpu_phys_addr_t;
#elif _WIN32
typedef unsigned long long bmvpu_phys_addr_t;
#endif

/* BmVpuAllocationFlags: flags for the BmVpuEncDMABufferAllocator's allocate vfunc */
typedef enum
{
    BM_VPU_ALLOCATION_FLAG_CACHED       = 0,
    BM_VPU_ALLOCATION_FLAG_WRITECOMBINE = 1,
    BM_VPU_ALLOCATION_FLAG_UNCACHED     = 2
} BmVpuAllocationFlags;

#define BM_VPU_ALLOCATION_FLAG_DEFAULT    BM_VPU_ALLOCATION_FLAG_WRITECOMBINE


typedef struct {
    unsigned int  size;
    uint64_t      phys_addr;
    uint64_t      virt_addr;
    int           enable_cache;
    int           dmabuf_fd;  //users cannot change it.
} BmVpuEncDMABuffer;

/**
 * Upload data from HOST to a VPU core.
 * For now, only support PCIE mode.
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_upload_data(int vpu_core_idx,
                      const uint8_t* host_va, int host_stride,
                      uint64_t vpu_pa, int vpu_stride,
                      int width, int height);

/**
 * Download data from a VPU core to HOST.
 * For now, only support PCIE mode.
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_download_data(int vpu_core_idx,
                        uint8_t* host_va, int host_stride,
                        uint64_t vpu_pa, int vpu_stride,
                        int width, int height);

/* Function pointer used during bmvpu_enc_open for allocate physical buffers.
 * vpu_core_idx: the buffer used by specified core
 * buf: the output physical buffer info
 * size: the input size for allocate physical buffer
*/
typedef void* (*BmVpuEncBufferAllocFunc)(void *context, int vpu_core_idx,
                                    BmVpuEncDMABuffer *buf, unsigned int size);
typedef void* (*BmVpuEncBufferFreeFunc)(void *context, int vpu_core_idx,
                                    BmVpuEncDMABuffer *buf);

/**
 * Alloc device memory according to the specified heap_id.
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_allocate(int vpu_core_idx, BmVpuEncDMABuffer *buf, unsigned int size);

/**
 * DeAlloc device memory.
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_deallocate(int vpu_core_idx, BmVpuEncDMABuffer *buf);

/**
 * Attach an externally allocated buffer
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_attach(int vpu_core_idx, uint64_t paddr, unsigned int size);

/**
 * Deattach an externally allocated buffer
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_deattach(int vpu_core_idx, uint64_t paddr, unsigned int size);


/**
 * Mmap operation
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_dma_buffer_map(int vpu_core_idx, BmVpuEncDMABuffer* buf, int port_flag);

/**
 * Munmap operation
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_dma_buffer_unmap(int vpu_core_idx, BmVpuEncDMABuffer* buf);

/**
 * flush operation
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_flush(int vpu_core_idx, BmVpuEncDMABuffer* buf);

/**
 * invalidate operation
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_dma_buffer_invalidate(int vpu_core_idx, BmVpuEncDMABuffer* buf);

DECL_EXPORT uint64_t bmvpu_enc_dma_buffer_get_physical_address(BmVpuEncDMABuffer* buf);
DECL_EXPORT unsigned int bmvpu_enc_dma_buffer_get_size(BmVpuEncDMABuffer* buf);


/******************************************************/
/******* MISCELLANEOUS STRUCTURES AND FUNCTIONS *******/
/******************************************************/


/* Frame types understood by the VPU. */
typedef enum
{
    BM_VPU_ENC_FRAME_TYPE_UNKNOWN = 0,
    BM_VPU_ENC_FRAME_TYPE_I,
    BM_VPU_ENC_FRAME_TYPE_P,
    BM_VPU_ENC_FRAME_TYPE_B,
    BM_VPU_ENC_FRAME_TYPE_IDR
} BmVpuEncFrameType;


/* Codec format to use for en/decoding. */
typedef enum
{
    /* H.264.
     * Encoding: Baseline/Constrained Baseline/Main/High/High 10 Profiles Level @ L5.2
     */
    BM_VPU_CODEC_FORMAT_H264,

    /* H.265.
     * Encoding: Supports Main/Main 10/Main Still Picture Profiles
     *           @ L5.1 High-tier
     */
    BM_VPU_CODEC_FORMAT_H265,
} BmVpuCodecFormat;

/* BmVpuMappingFlags: flags for the BmVpuDMABufferAllocator's map function pointers
 * These flags can be bitwise-OR combined, although READ and WRITE cannot
 * both be set */
typedef enum
{
    /* Map memory for CPU write access */
    BM_VPU_ENC_MAPPING_FLAG_WRITE   = (1UL << 0),
    /* Map memory for CPU read access */
    BM_VPU_ENC_MAPPING_FLAG_READ    = (1UL << 1)
} BmVpuEncMappingFlags;

typedef enum
{
    BM_VPU_ENC_PIX_FORMAT_YUV420P = 0,   /* planar 4:2:0 chroma_interleave is 0;*/
    BM_VPU_ENC_PIX_FORMAT_YUV422P = 1,   /* enc not support.*/
    BM_VPU_ENC_PIX_FORMAT_YUV444P = 3,   /* enc not support.*/
    BM_VPU_ENC_PIX_FORMAT_YUV400  = 4,   /* enc not support 8-bit greayscale */
    BM_VPU_ENC_PIX_FORMAT_NV12    = 5,   /* planar 4:2:0 chroma_interleave is 1;*/
    BM_VPU_ENC_PIX_FORMAT_NV21    = 8,   /* planar 4:2:0 chroma_interleave is 1;*/
    BM_VPU_ENC_PIX_FORMAT_NV16    = 6,   /* enc not support.*/
    BM_VPU_ENC_PIX_FORMAT_NV24    = 7,   /* enc not support.*/
} BmVpuEncPixFormat;



/* Framebuffers are frame containers, and are used both for en- and decoding. */
typedef struct
{
    /* DMA buffer which contains the pixels. */
    BmVpuEncDMABuffer *dma_buffer;

    /* Make sure each framebuffer has an ID that is different
     * to the IDs of each other */
    int          myIndex;

    /* Stride of the Y and of the Cb&Cr components.
     * Specified in bytes. */
    unsigned int y_stride;
    unsigned int cbcr_stride; // TODO

    unsigned int width;  /* width of frame buffer */
    unsigned int height; /* height of frame buffer */

    /* These define the starting offsets of each component
     * relative to the start of the buffer. Specified in bytes. */
    size_t y_offset;
    size_t cb_offset;
    size_t cr_offset;

    /* Set to 1 if the framebuffer was already marked as used in encoder.
     * This is for internal use only.
     * Not to be read or written from the outside. */
    int already_marked;

    /* Internal, implementation-defined data. Do not modify. */
    void *internal;

    /* User-defined pointer.
     * The library does not touch this value.
     * This can be used for example to identify which framebuffer out of
     * the initially allocated pool was used by the VPU to contain a frame.
     */
    void *context;
} BmVpuFramebuffer;

/* Structure containing details about encoded frames. */
typedef struct
{
    /* When decoding, data must point to the memory block which contains
     * encoded frame data that gets consumed by the VPU.
     * Only used by the decoder. */
    uint8_t *data;

    /* Size of the encoded data, in bytes. When decoding, this is set by
     * the user, and is the size of the encoded data that is pointed to
     * by data. When encoding, the encoder sets this to the size of the
     * acquired output block, in bytes (exactly the same value as the
     * acquire_output_buffer's size argument). */
    size_t data_size;
    // size_t data_len;

    /* Frame type (I, P, B, ..) of the encoded frame. Filled by the encoder.
     * Only used by the encoder. */
    BmVpuEncFrameType frame_type;

    /* Handle produced by the user-defined acquire_output_buffer function
     * during encoding.
     * Only used by the encoder. */
    void *acquired_handle;

    /* User-defined pointer.
     * The library does not touch this value.
     * This pointer and the one from the corresponding raw frame will have
     * the same value. The library will pass then through. */
    void *context;

    /* User-defined timestamps.
     * In many cases, the context one wants to associate with raw/encoded frames
     * is a PTS-DTS pair. Just like the context pointer, the library just passes
     * them through to the associated raw frame, and does not actually touch
     * their values. */
    uint64_t pts;
    uint64_t dts;

    int src_idx;

    // for roi index
    bmvpu_phys_addr_t u64CustomMapPhyAddr;

    int avg_ctu_qp;
} BmVpuEncodedFrame;


/* Structure containing details about raw, uncompressed frames. */
typedef struct
{
    BmVpuFramebuffer *framebuffer;

    /* User-defined pointer.
     * The library does not touch this value.
     * This pointer and the one from the corresponding encoded frame will have
     * the same value. The library will pass then through. */
    void *context;

    /* User-defined timestamps.
     * In many cases, the context one wants to associate with raw/encoded frames
     * is a PTS-DTS pair. Just like the context pointer, the library just passes
     * them through to the associated encoded frame, and does not actually touch
     * their values. */
    uint64_t pts;
    uint64_t dts;
} BmVpuRawFrame;

typedef struct {
    /* Frame width and height, aligned to the 16-pixel boundary required by the VPU. */
    int width;
    int height;

    /* Stride sizes, in bytes, with alignment applied.
     * The Cb and Cr planes always use the same stride. */
    int y_stride; /* aligned stride */
    int c_stride; /* aligned stride (optional) */

    /* Required DMA memory size for the Y,Cb,Cr planes, in bytes.
     * The Cb and Cr planes always are of the same size. */
    int y_size;
    int c_size;

    /* Total required size of a framebuffer's DMA buffer, in bytes.
     * This value includes the sizes of all planes. */
    int size;
} BmVpuFbInfo;





/* Encoder return codes. With the exception of BM_VPU_ENC_RETURN_CODE_OK, these
 * should be considered hard errors, and the encoder should be closed when they
 * are returned. */
typedef enum
{
    /* Operation finished successfully. */
    BM_VPU_ENC_RETURN_CODE_OK = 0,
    /* General return code for when an error occurs. This is used as a catch-all
     * for when the other error return codes do not match the error. */
    BM_VPU_ENC_RETURN_CODE_ERROR,
    /* Input parameters were invalid. */
    BM_VPU_ENC_RETURN_CODE_INVALID_PARAMS,
    /* VPU encoder handle is invalid. This is an internal error, and most likely
     * a bug in the library. Please report such errors. */
    BM_VPU_ENC_RETURN_CODE_INVALID_HANDLE,
    /* Framebuffer information is invalid. Typically happens when the BmVpuFramebuffer
     * structures that get passed to bmvpu_enc_register_framebuffers() contain
     * invalid values. */
    BM_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER,
    /* Registering framebuffers for encoding failed because not enough framebuffers
     * were given to the bmvpu_enc_register_framebuffers() function. */
    BM_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
    /* A stride value (for example one of the stride values of a framebuffer) is invalid. */
    BM_VPU_ENC_RETURN_CODE_INVALID_STRIDE,
    /* A function was called at an inappropriate time. */
    BM_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE,
    /* The operation timed out. */
    BM_VPU_ENC_RETURN_CODE_TIMEOUT,
    /* resend frame*/
    BM_VPU_ENC_RETURN_CODE_RESEND_FRAME,
    /* encode end*/
    BM_VPU_ENC_RETURN_CODE_ENC_END,
    /* The encoding end. */
    BM_VPU_ENC_RETURN_CODE_END
} BmVpuEncReturnCodes;


/* Encoder output codes. These can be bitwise OR combined, so check
 * for their presence in the output_codes bitmask returned by
 * bmvpu_enc_encode() by using a bitwise AND. */
typedef enum
{
    /* Input data was used. If this code is not present, then the encoder
     * didn't use it yet, so give it to the encoder again until this
     * code is set or an error is returned. */
    BM_VPU_ENC_OUTPUT_CODE_INPUT_USED                 = (1UL << 0),
    /* A fully encoded frame is now available. The encoded_frame argument
     * passed to bmvpu_enc_encode() contains information about this frame. */
    BM_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE    = (1UL << 1),
    /* The data in the encoded frame also contains header information
     * like SPS/PSS for h.264. Headers are always placed at the beginning
     * of the encoded data, and this code is never present if the
     * BM_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE isn't set. */
    BM_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER            = (1UL << 2)
} BmVpuEncOutputCodes;


typedef enum
{
    BM_VPU_ENC_HEADER_DATA_TYPE_VPS_RBSP = 0,
    BM_VPU_ENC_HEADER_DATA_TYPE_SPS_RBSP,
    BM_VPU_ENC_HEADER_DATA_TYPE_PPS_RBSP
} BmVpuEncHeaderDataTypes;

enum {
    BM_LINEAR_FRAME_MAP      = 0, /* Linear frame map type */
    BM_COMPRESSED_FRAME_MAP  = 10 /* Compressed frame map type */
};

/**
 * @brief   This is a special enumeration type for defining GOP structure presets.
 */
typedef enum {
    BM_VPU_ENC_GOP_PRESET_ALL_I  = 1, /**< All Intra, gopsize = 1 */
    BM_VPU_ENC_GOP_PRESET_IPP    = 2, /**< Consecutive P, cyclic gopsize = 1 */
    BM_VPU_ENC_GOP_PRESET_IBBB   = 3, /**< Consecutive B, cyclic gopsize = 1 */
    BM_VPU_ENC_GOP_PRESET_IBPBP  = 4, /**< gopsize = 2 */
    BM_VPU_ENC_GOP_PRESET_IBBBP  = 5, /**< gopsize = 4 */
    BM_VPU_ENC_GOP_PRESET_IPPPP  = 6, /**< Consecutive P, cyclic gopsize = 4 */
    BM_VPU_ENC_GOP_PRESET_IBBBB  = 7, /**< Consecutive B, cyclic gopsize = 4 */
    BM_VPU_ENC_GOP_PRESET_RA_IB  = 8, /**< Random Access, cyclic gopsize = 8 */
} BMVpuEncGopPreset;

typedef enum {
    BM_VPU_ENC_CUSTOM_MODE      = 0, // Custom mode,
    BM_VPU_ENC_RECOMMENDED_MODE = 1, // recommended encoder parameters (slow encoding speed, highest picture quality)
    BM_VPU_ENC_BOOST_MODE       = 2, // Boost mode (normal encoding speed, normal picture quality),
    BM_VPU_ENC_FAST_MODE        = 3, // Fast mode (high encoding speed, low picture quality) */
} BMVpuEncMode;

/* h.264 parameters for the new encoder instance. */
typedef struct
{
    /* enables 8x8 intra prediction and 8x8 transform.
     * Default value is 1. */
    int enable_transform8x8;
} BmVpuEncH264Params;

/* h.265 parameters for the new encoder instance. */
typedef struct
{
    /* Enable temporal motion vector prediction.
     * Default value is 1. */
    int enable_tmvp;

    /* Enable Wave-front Parralel Processing for linear buffer mode.
     * Default value is 0. */
    int enable_wpp;

    /* If set to 1, SAO is enabled. If set to 0, it is disabled.
     * Default value is 1. */
    int enable_sao;

    /* Enable strong intra smoothing for intra blocks to prevent artifacts
     * in regions with few AC coefficients.
     * Default value is 1.  */
    int enable_strong_intra_smoothing;

    /* Enable transform skip for an intra CU.
     * Default value is 0. */
    int enable_intra_trans_skip;

    /* Enable intra NxN PUs.
     * Default value is 1. */
    int enable_intraNxN;
} BmVpuEncH265Params;


/* Structure used together with bmvpu_enc_open() */
typedef struct
{
    /* Format encoded data to produce. */
    BmVpuCodecFormat codec_format;

    /* Color format to use for incoming frames.
     * Video codec formats only allow for the two formats
     * BM_VPU_ENC_PIX_FORMAT_YUV420P and BM_VPU_COLOR_FORMAT_YUV400 (the second
     * one is supported by using YUV420 and dummy U and V planes internally).
     * See the BmVpuEncPixFormat documentation. */
    BmVpuEncPixFormat pix_format;

    /* Width and height of the incoming frames, in pixels. These
     * do not have to be aligned to any boundaries. */
    uint32_t frame_width;
    uint32_t frame_height;

    /* Time base, given as a rational number. */
    /* numerator/denominator */
    uint32_t timebase_num;
    uint32_t timebase_den;

    /* Frame rate, given as a rational number. */
    /* numerator/denominator */
    uint32_t fps_num;
    uint32_t fps_den;

    /* Bitrate in bps. If this is set to 0, rate control is disabled, and
     * constant quality mode is active instead.
     * Default value is 100000. */
    int64_t bitrate;

    /* Size of the vbv buffer, in bits. This is only used if rate control is active
     * (= the bitrate in BmVpuEncOpenParams is nonzero).
     * 0 means the buffer size constraints are not checked for.
     * Default value is 0. */
    uint64_t vbv_buffer_size;

    /* the quantization parameter for constant quality mode */
    int cqp;

    /* 0 : Custom mode,
     * 1 : recommended encoder parameters (slow encoding speed, highest picture quality)
     * 2 : Boost mode (normal encoding speed, normal picture quality),
     * 3 : Fast mode (high encoding speed, low picture quality) */
    BMVpuEncMode enc_mode;

    /* The number of merge candidates in RDO(1 or 2).
     *  1: improve encoding performance.
     *  2: improve quality of encoded picture;
     *  Default value is 2. */
    int max_num_merge;

    /* If set to 1, constrained intra prediction is enabled.
     * If set to 0, it is disabled.
     * Default value is 0. */
    int enable_constrained_intra_prediction;

    /* Enable weighted prediction
     * Default value is 1. */
    int enable_wp;

    /* If set to 1, the deblocking filter is disabled.
     * If set to 0, it remains enabled.
     * Default value is 0. */
    int disable_deblocking;
    /* Alpha/Tc offset for the deblocking filter.
     * Default value is 0. */
    int offset_tc;
    /* Beta offset for the deblocking filter.
     * Default value is 0. */
    int offset_beta;
    /* Enable filtering cross slice boundaries in in-loop debelocking.
     * Default value is 0. */
    int enable_cross_slice_boundary;

    /* Enable Noise Reduction
     * Default value is 1.*/
    int enable_nr;

    /* Additional codec format specific parameters. */
    union
    {
        BmVpuEncH264Params h264_params;
        BmVpuEncH265Params h265_params;
    };


    /* only used for PCIE mode. For SOC mode, this must be 0.
     * Default value is 0. */
    int soc_idx;

    /* A GOP structure preset option
     * 1: all I,          all Intra, gopsize = 1
     * 2: I-P-P,          consecutive P, cyclic gopsize = 1
     * 3: I-B-B-B,        consecutive B, cyclic gopsize = 1
     * 4: I-B-P-B-P,      gopsize = 2
     * 5: I-B-B-B-P,      gopsize = 4
     * 6: I-P-P-P-P,      consecutive P, cyclic gopsize = 4
     * 7: I-B-B-B-B,      consecutive B, cyclic gopsize = 4
     * 8: Random Access, I-B-B-B-B-B-B-B-B, cyclic gopsize = 8
     * Low delay cases are 1, 2, 3, 6, 7.
     * Default value is 5. */
    BMVpuEncGopPreset gop_preset;

    // TODO
    /* A period of intra picture in GOP size.
     * Default value is 28. */
    int intra_period;

    /* Enable background detection.
     * Default value is 0 */
    int bg_detection;

    /* Enable MB-level/CU-level rate control.
     * Default value is 1 */
    int mb_rc;

    /* maximum delta QP for rate control
     * Default value is 5 */
    int delta_qp;

    /* minimum QP for rate control
     * Default value is 8 */
    int min_qp;

    /* maximum QP for rate control
     * Default value is 51 */
    int max_qp;

    /* roi encoding flag
     * Default value is 0 */
    int roi_enable;

    /* set cmd queue depath
     * Default value is 4
     * the value must 1 <= value <= 4*/
    int cmd_queue_depth;

    int timeout;
    int timeout_count;

    BmVpuEncBufferAllocFunc buffer_alloc_func;
    BmVpuEncBufferFreeFunc buffer_free_func;
    void *buffer_context;
} BmVpuEncOpenParams;

/* Initial encoding information, produced by the encoder. This structure is
 * essential to actually begin encoding, since it contains all of the
 * necessary information to create and register enough framebuffers. */
typedef struct
{
    /* Caller must register at least this many framebuffers for reconstruction */
    uint32_t min_num_rec_fb;

    /* Caller must register at least this many framebuffers for source(GOP) */
    uint32_t min_num_src_fb;

    /* Physical framebuffer addresses must be aligned to this value. */
    uint32_t framebuffer_alignment;

    /* frame buffer size for reconstruction */
    BmVpuFbInfo rec_fb;

    /* frame buffer size for source */
    BmVpuFbInfo src_fb;

} BmVpuEncInitialInfo;


/* Function pointer used during encoding for acquiring output buffers.
 * See bmvpu_enc_encode() for details about the encoding process.
 * context is the value of output_buffer_context specified in
 * BmVpuEncParams. size is the size of the block to acquire, in bytes.
 * acquired_handle is an output value; the function can set this to a
 * handle that corresponds to the acquired buffer. For example, in
 * libav/FFmpeg, this handle could be a pointer to an AVBuffer. In
 * GStreamer, this could be a pointer to a GstBuffer. The value of
 * *acquired_handle will later be copied to the acquired_handle value
 * of BmVpuEncodedFrame.
 * The return value is a pointer to a memory-mapped region of the
 * output buffer, or NULL if acquiring failed.
 * This function is only used by bmvpu_enc_encode(). */
typedef void* (*BmVpuEncAcquireOutputBuffer)(void *context, size_t size, void **acquired_handle);
/* Function pointer used during encoding for notifying that the encoder
 * is done with the output buffer. This is *not* a function for freeing
 * allocated buffers; instead, it makes it possible to release, unmap etc.
 * context is the value of output_buffer_context specified in
 * BmVpuEncParams. acquired_handle equals the value of *acquired_handle in
 * BmVpuEncAcquireOutputBuffer. */
typedef void (*BmVpuEncFinishOutputBuffer)(void *context, void *acquired_handle);


/* Custom map options in H.265/HEVC encoder. */
typedef struct {
    /* Set an average QP of ROI map. */
    int roiAvgQp;

    /* Enable ROI map. */
    int customRoiMapEnable;
    /* Enable custom lambda map. */
    int customLambdaMapEnable;
    /* Force CTU to be encoded with intra or to be skipped.  */
    int customModeMapEnable;
    /* Force all coefficients to be zero after TQ or not for each CTU (to be dropped).*/
    int customCoefDropEnable;

    /**
     * It indicates the start buffer address of custom map.
     * Each custom CTU map takes 8 bytes and holds mode,
     * coefficient drop flag, QPs, and lambdas like the below illustration.
     * image::../figure/wave520_ctumap.svg["Format of custom Map", width=300]
     */
    bmvpu_phys_addr_t addrCustomMap;
} BmCustomMapOpt;


typedef struct
{
    /* If set to 1, the VPU ignores the given source frame, and
     * instead generates a "skipped frame". If such a frame is
     * reconstructed, it is a duplicate of the preceding frame.
     * This skipped frame is encoded as a P frame.
     * 0 disables skipped frame generation. Default value is 0. */
    int skip_frame;

    /* A flag to use a force picture type */
    int       forcePicTypeEnable;
    /* A force picture type (I, P, B, IDR, CRA).
     * It is valid when forcePicTypeEnable is 1. */
    int       forcePicType;

    /* Functions for acquiring and finishing output buffers. See the
     * typedef documentations above for details about how these
     * functions should behave, and the bmvpu_enc_encode()
     * documentation for how they are used. */
    BmVpuEncAcquireOutputBuffer acquire_output_buffer;
    BmVpuEncFinishOutputBuffer  finish_output_buffer;

    /* User supplied value that will be passed to the functions */
    void *output_buffer_context;

    /* roi custom map info */
    BmCustomMapOpt* customMapOpt;
} BmVpuEncParams;

/* BM VPU Encoder structure. */
typedef struct
{
    void* handle;

    int soc_idx; /* The index of Sophon SoC.
                  * For PCIE mode, please refer to the number at /dev/bm-sophonxx.
                  * For SOC mode, set it to zero. */
    int core_idx; /* unified index for vpu encoder cores at all Sophon SoCs */

    BmVpuCodecFormat codec_format;
    BmVpuEncPixFormat pix_format;

    uint32_t frame_width;
    uint32_t frame_height;

    uint32_t fps_n;
    uint32_t fps_d;

    int first_frame;

    int rc_enable;
    /* constant qp when rc is disabled */
    int cqp;

    /* DMA buffer for working */
    BmVpuEncDMABuffer*   work_dmabuffer;

    /* DMA buffer for bitstream */
    BmVpuEncDMABuffer* bs_dmabuffer;

    unsigned long long bs_virt_addr;
    bmvpu_phys_addr_t bs_phys_addr;

    /* DMA buffer for frame data */
    uint32_t      num_framebuffers;
    void * /*VpuFrameBuffer**/   internal_framebuffers;
    BmVpuFramebuffer* framebuffers;

    /* TODO change all as the parameters of bmvpu_enc_register_framebuffers() */
    /* DMA buffer for colMV */
    BmVpuEncDMABuffer*   buffer_mv;

    /* DMA buffer for FBC luma table */
    BmVpuEncDMABuffer*   buffer_fbc_y_tbl;

    /* DMA buffer for FBC chroma table */
    BmVpuEncDMABuffer*   buffer_fbc_c_tbl;

    /* Sum-sampled DMA buffer for ME */
    BmVpuEncDMABuffer*   buffer_sub_sam;

    uint8_t* headers_rbsp;
    size_t   headers_rbsp_size;

    BmVpuEncInitialInfo initial_info;

    int timeout;
    int timeout_count;

    /* internal use */
    void *video_enc_ctx;
} BmVpuEncoder;

/* Returns a human-readable description of the error code.
 * Useful for logging. */
DECL_EXPORT char const * bmvpu_enc_error_string(BmVpuEncReturnCodes code);

/* Get the unified core index for VPU encoder at a specified Sophon SoC. */
DECL_EXPORT int bmvpu_enc_get_core_idx(int soc_idx);

/* These two functions load/unload the encoder. Due to an internal reference
 * counter, it is safe to call these functions more than once. However, the
 * number of unload() calls must match the number of load() calls.
 *
 * The encoder must be loaded before doing anything else with it.
 * Similarly, the encoder must not be unloaded before all encoder activities
 * have been finished. This includes opening/decoding encoder instances. */
DECL_EXPORT int bmvpu_enc_load(int soc_idx);
DECL_EXPORT int bmvpu_enc_unload(int soc_idx);

DECL_EXPORT int bmvpu_get_ext_addr(void);

/* Called before bmvpu_enc_open(), it returns the alignment and size for the
 * physical memory block necessary for the encoder's bitstream buffer.
 * The user must allocate a DMA buffer of at least this size, and its physical
 * address must be aligned according to the alignment value. */
DECL_EXPORT void bmvpu_enc_get_bitstream_buffer_info(size_t *size, uint32_t *alignment);

/* Set the fields in "open_params" to valid defaults
 * Useful if the caller wants to modify only a few fields (or none at all) */
DECL_EXPORT void bmvpu_enc_set_default_open_params(BmVpuEncOpenParams *open_params, BmVpuCodecFormat codec_format);


/**
 * Fill fields of the BmVpuFramebuffer structure, based on data from "fb_info".
 * The specified DMA buffer and context pointer are also set.
 */
DECL_EXPORT int bmvpu_fill_framebuffer_params(BmVpuFramebuffer *framebuffer,
                                   BmVpuFbInfo *fb_info,
                                   BmVpuEncDMABuffer *fb_dma_buffer,
                                   int fb_id, void* context);


/* Opens a new encoder instance.
 * "open_params" and "bs_dmabuffer" must not be NULL. */
DECL_EXPORT int bmvpu_enc_open(BmVpuEncoder **encoder, BmVpuEncOpenParams *open_params,
                            BmVpuEncDMABuffer *bs_dmabuffer, BmVpuEncInitialInfo *initial_info);

/* Closes a encoder instance.
 * Trying to close the same instance multiple times results in undefined behavior. */
DECL_EXPORT int bmvpu_enc_close(BmVpuEncoder *encoder);

/* Encodes a given raw input frame with the given encoding parameters.
 * encoded_frame is filled with information about the resulting encoded output frame.
 * The encoded frame data itself is stored in a buffer that is allocated
 * by user-supplied functions (which are set as the acquire_output_buffer and
 * finish_output_buffer function pointers in the encoding_params).
 *
 * Encoding internally works as follows:
 * first, the actual encoding operation is performed by the VPU.
 * Next, information about the encoded data is queried, particularly its size in bytes.
 * Once this size is known, acquire_output_buffer() from encoding_params is called.
 * This function must acquire a buffer that can be used to store the encoded data.
 * This buffer must be at least as large as the size of the encoded data
 * (which is given to acquire_output_buffer() as an argument). The return value of acquire_output_buffer()
 * is a pointer to the (potentially memory-mapped) region of the buffer. The encoded frame data is then
 * copied to this buffer, and finish_output_buffer() is called. This function can be used to inform the
 * caller that the encoder is done with this buffer; it now contains encoded data, and will not be modified
 * further. encoded_frame is filled with information about the encoded frame data.
 * If acquiring the buffer fails, acquire_output_buffer() returns a NULL pointer.
 *
 * NOTE: again, finish_output_buffer() is NOT a function to free the buffer; it just signals that the encoder
 * won't touch the memory inside the buffer anymore.
 *
 * acquire_output_buffer() can also pass on a handle to the acquired buffer (for example, in FFmpeg/libav,
 * this handle would be a pointer to an AVBuffer). The handle is called the "acquired_handle".
 * acquire_output_buffer() can return such a handle. This handle is copied to the encoded_frame struct's
 * acquired_handle field. This way, a more intuitive workflow can be used; if for example, acquire_output_buffer()
 * returns an AVBuffer pointer as the handle, this AVBuffer pointer ends up in the encoded_frame. Afterwards,
 * encoded_frame contains all the necessary information to process the encoded frame data.
 *
 * It is guaranteed that once the buffer was acquired, finish_output_buffer() will always be called, even if
 * an error occurs. This prevents potential memory/resource leaks if the finish_output_buffer() call somehow
 * unlocks or releases the buffer for further processing. The acquired_handle is also copied to encoded_frame
 * even if an error occurs, unless the error occurred before the acquire_output_buffer() call, in which case
 * the encoded_frame's acquired_handle field will be set to NULL.
 *
 * The other fields in encoding_params specify additional encoding parameters, which can vary from frame to
 * frame.
 * output_code is a bit mask containing information about the encoding result. The value is a bitwise OR
 * combination of the codes in BmVpuEncOutputCodes.
 *
 * None of the arguments may be NULL. */

DECL_EXPORT int bmvpu_enc_send_frame(BmVpuEncoder *encoder,
                     BmVpuRawFrame const *raw_frame,
                     BmVpuEncParams *encoding_params);

DECL_EXPORT int bmvpu_enc_get_stream(BmVpuEncoder *encoder,
                     BmVpuEncodedFrame *encoded_frame,
                     BmVpuEncParams *encoding_params);



DECL_EXPORT int bmvpu_enc_encode(BmVpuEncoder *encoder,
                     BmVpuRawFrame const *raw_frame,
                     BmVpuEncodedFrame *encoded_frame,
                     BmVpuEncParams *encoding_params,
                     uint32_t *output_code);


/**
 * Returns a human-readable description of the given color format.
 * Useful for logging.
 */
char const *bmvpu_pix_format_string(BmVpuEncPixFormat pix_format);

/**
 * Returns a human-readable description of the given frame type.
 * Useful for logging.
 */
char const *bmvpu_frame_type_string(BmVpuEncFrameType frame_type);


/**
 * Parse the parameters in string
 *
 * return value:
 *   -1, failed
 *    0, done
 */
DECL_EXPORT int bmvpu_enc_param_parse(BmVpuEncOpenParams *p, const char *name, const char *value);




#ifdef __cplusplus
}
#endif


#endif
