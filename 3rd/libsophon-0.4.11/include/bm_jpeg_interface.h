#ifndef __BM_JPEG_INTERFACE_H__
#define __BM_JPEG_INTERFACE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "bmlib_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined DECL_EXPORT
#ifdef _WIN32
    #define DECL_EXPORT __declspec(dllexport)
#else
    #define DECL_EXPORT
#endif
#endif

/* This library provides a high-level interface for controlling the BitMain JPU en/decoder.
 *
 * Note that the functions are _not_ thread safe. If they may be called from
 * different threads, you must make sure they are surrounded by a mutex lock.
 * It is recommended to use one global mutex for the bm_jpu_*_load()/unload()
 * functions, and another de/encoder instance specific mutex for all of the other
 * calls. */

/* Format and for printf-compatible format-strings
 * example use: printf("physical address: %" BM_JPU_PHYS_ADDR_FORMAT, phys_addr */
#define BM_JPU_PHYS_ADDR_FORMAT "#lx"
/* Typedef for physical addresses */
typedef unsigned long long bm_jpu_phys_addr_t;

/* Heap allocation function for virtual memory blocks internally allocated by bmjpuapi.
 * These have nothing to do with the DMA buffer allocation interface defined above.
 * By default, malloc/free are used. */
typedef void* (*BmJpuHeapAllocFunc)(size_t const size, void *context, char const *file, int const line, char const *fn);
typedef void  (*BmJpuHeapFreeFunc)(void *memblock, size_t const size, void *context, char const *file, int const line, char const *fn);

/* This function allows for setting custom heap allocators, which are used to create internal heap blocks.
 * The heap allocator referred to by "heap_alloc_fn" must return NULL if allocation fails.
 * "context" is a user-defined value, which is passed on unchanged to the allocator functions.
 * Calling this function with either "heap_alloc_fn" or "heap_free_fn" set to NULL resets the internal
 * pointers to use malloc and free (the default allocators). */
DECL_EXPORT void bm_jpu_set_heap_allocator_functions(BmJpuHeapAllocFunc heap_alloc_fn, BmJpuHeapFreeFunc heap_free_fn, void *context);

/* Common structures and functions */
/* Log levels. */
typedef enum {
    BM_JPU_LOG_LEVEL_ERROR   = 0,
    BM_JPU_LOG_LEVEL_WARNING = 1,
    BM_JPU_LOG_LEVEL_INFO    = 2,
    BM_JPU_LOG_LEVEL_DEBUG   = 3,
    BM_JPU_LOG_LEVEL_LOG     = 4,
    BM_JPU_LOG_LEVEL_TRACE   = 5
} BmJpuLogLevel;

/* Function pointer type for logging functions.
 *
 * This function is invoked by BM_JPU_LOG() macro calls. This macro also passes the name
 * of the source file, the line in that file, and the function name where the logging occurs
 * to the logging function (over the file, line, and fn arguments, respectively).
 * Together with the log level, custom logging functions can output this metadata, or use
 * it for log filtering etc. */
typedef void (*BmJpuLoggingFunc)(BmJpuLogLevel level, char const *file, int const line, char const *fn, const char *format, ...);

/* Defines the threshold for logging. Logs with lower priority are discarded.
 * By default, the threshold is set to BM_JPU_LOG_LEVEL_INFO. */
DECL_EXPORT void bm_jpu_set_logging_threshold(BmJpuLogLevel threshold);

/* Defines a custom logging function.
 * If logging_fn is NULL, logging is disabled. This is the default value. */
DECL_EXPORT void bm_jpu_set_logging_function(BmJpuLoggingFunc logging_fn);

typedef enum {
    BM_JPU_IMAGE_FORMAT_YUV420P = 0,
    BM_JPU_IMAGE_FORMAT_YUV422P,
    BM_JPU_IMAGE_FORMAT_YUV444P,
    BM_JPU_IMAGE_FORMAT_NV12,
    BM_JPU_IMAGE_FORMAT_NV21,
    BM_JPU_IMAGE_FORMAT_NV16,
    BM_JPU_IMAGE_FORMAT_NV61,
    BM_JPU_IMAGE_FORMAT_GRAY,
    BM_JPU_IMAGE_FORMAT_RGB  /* for opencv */
} BmJpuImageFormat;

typedef enum {
    /* planar 4:2:0; if the chroma_interleave parameter is 1, the corresponding format is NV12, otherwise it is I420 */
    BM_JPU_COLOR_FORMAT_YUV420            = 0,
    /* planar 4:2:2; if the chroma_interleave parameter is 1, the corresponding format is NV16 */
    BM_JPU_COLOR_FORMAT_YUV422_HORIZONTAL = 1,
    /* 4:2:2 vertical, actually 2:2:4 (according to the JPU docs); no corresponding format known for the chroma_interleave=1 case */
    /* NOTE: this format is rarely used, and has only been seen in a few JPEG files */
    BM_JPU_COLOR_FORMAT_YUV422_VERTICAL   = 2,
    /* planar 4:4:4; if the chroma_interleave parameter is 1, the corresponding format is NV24 */
    BM_JPU_COLOR_FORMAT_YUV444            = 3,
    /* 8-bit greayscale */
    BM_JPU_COLOR_FORMAT_YUV400            = 4,
    /* BUTT */
    BM_JPU_COLOR_FORMAT_BUTT
} BmJpuColorFormat;

typedef enum {
    BM_JPU_CHROMA_FORMAT_CBCR_SEPARATED = 0,
    BM_JPU_CHROMA_FORMAT_CBCR_INTERLEAVE = 1,
    BM_JPU_CHROMA_FORMAT_CRCB_INTERLEAVE = 2
} BmJpuChromaFormat;

/* Not supported yet */
typedef enum {
    BM_JPU_PACKED_FORMAT_NONE = 0,
    BM_JPU_PACKED_FORMAT_422_YUYV = 1,
    BM_JPU_PACKED_FORMAT_422_UYVY = 2,
    BM_JPU_PACKED_FORMAT_422_YVYU = 3,
    BM_JPU_PACKED_FORMAT_422_VYUY = 4,
    BM_JPU_PACKED_FORMAT_444 = 5
} BmJpuPackedFormat;

/* Note: Rotate in counterclockwise */
typedef enum {
    BM_JPU_ROTATE_NONE = 0,
    BM_JPU_ROTATE_90 = 90,
    BM_JPU_ROTATE_180 = 180,
    BM_JPU_ROTATE_270 = 270
} BmJpuRotateAngle;

typedef enum {
    BM_JPU_MIRROR_NONE = 0,
    BM_JPU_MIRROR_VER = 1,
    BM_JPU_MIRROR_HOR = 2,
    BM_JPU_MIRROR_HOR_VER = 3
} BmJpuMirrorDirection;

/* Structure used together with bm_jpu_calc_framebuffer_sizes() */
typedef struct {
    /* Frame width and height, aligned to the 16-pixel boundary required by the JPU. */
    unsigned int aligned_frame_width, aligned_frame_height;

    /* Stride sizes, in bytes, with alignment applied. The Cb and Cr planes always
     * use the same stride, so they share the same value. */
    unsigned int y_stride, cbcr_stride;

    /* Required DMA memory size for the Y,Cb,Cr planes in bytes.
     * The Cb and Cr planes always are of the same size, so they share the same value. */
    unsigned int y_size, cbcr_size;

    /* Total required size of a framebuffer's DMA buffer, in bytes. This value includes
     * the sizes of all planes, and extra bytes for alignment and padding.
     * This value must be used when allocating DMA buffers for decoder framebuffers. */
    unsigned int total_size;

    /* Image format of frame.
     * It is stored here to allow other functions to select the correct offsets. */
    BmJpuImageFormat image_format;
} BmJpuFramebufferSizes;

/* Returns a human-readable description of the given color format. Useful for logging. */
DECL_EXPORT char const *bm_jpu_color_format_string(BmJpuColorFormat color_format);

/* Returns a human-readable description of the given image format. Useful for logging. */
DECL_EXPORT char const *bm_jpu_image_format_string(BmJpuImageFormat image_format);

DECL_EXPORT int bm_jpu_hw_reset();

/* Framebuffers are frame containers, and are used both for en- and decoding. */
typedef struct {
    /* Stride of the Y and of the Cb&Cr components.
     * Specified in bytes. */
    unsigned int y_stride;
    unsigned int cbcr_stride;

    /* DMA buffer which contains the pixels. */
    bm_device_mem_t *dma_buffer;

    /* These define the starting offsets of each component
     * relative to the start of the buffer. Specified in bytes.
     */
    size_t y_offset;
    size_t cb_offset;
    size_t cr_offset;

    /* User-defined pointer. The library does not touch this value.
     * Not to be confused with the context fields of BmJpuEncodedFrame
     * and BmJpuRawFrame.
     * This can be used for example to identify which framebuffer out of
     * the initially allocated pool was used by the JPU to contain a frame.
     */
    void *context;

    /* Set to 1 if the framebuffer was already marked as displayed. This is for
     * internal use only. Not to be read or written from the outside. */
    int already_marked;

    /* Internal, implementation-defined data. Do not modify. */
    void *internal;
} BmJpuFramebuffer;

/* Structure containing details about encoded frames. */
typedef struct {
    /* When decoding, data must point to the memory block which contains
     * encoded frame data that gets consumed by the JPU. Not used by
     * the encoder. */
    uint8_t *data;

    /* Size of the encoded data, in bytes. When decoding, this is set by
     * the user, and is the size of the encoded data that is pointed to
     * by data. When encoding, the encoder sets this to the size of the
     * acquired output block, in bytes (exactly the same value as the
     * acquire_output_buffer's size argument). */
    size_t data_size;

    /* Handle produced by the user-defined acquire_output_buffer function
     * during encoding. Not used by the decoder. */
    void *acquired_handle;

    /* User-defined pointer. The library does not touch this value.
     * This pointer and the one from the corresponding raw frame will have
     * the same value. The library will pass then through.
     * It can be used to identify which raw frame is associated with this
     * encoded frame for example. */
    void *context;

    /* User-defined timestamps. These are here for convenience. In many
     * cases, the context one wants to associate with raw/encoded frames
     * is a PTS-DTS pair. If only the context pointer were available, users
     * would have to create a separate data structure containing PTS & DTS
     * values for each context. Since this use case is common, these two
     * fields are added to the frame structure. Just like the context
     * pointer, the library just passes them through to the associated
     * raw frame, and does not actually touch their values. It is also
     * perfectly OK to not use them, and just use the context pointer
     * instead, or vice versa. */
    uint64_t pts, dts;
} BmJpuEncodedFrame;

/* Structure containing details about raw, uncompressed frames. */
typedef struct {
    /* When decoding: pointer to the framebuffer containing the decoded raw frame.
     * When encoding: pointer to the framebuffer containing the raw frame to encode. */
    BmJpuFramebuffer *framebuffer;

    /* User-defined pointer. The library does not touch this value.
     * This pointer and the one from the corresponding encoded frame will have
     * the same value. The library will pass then through.
     * It can be used to identify which raw frame is associated with this
     * encoded frame for example. */
    void *context;

    /* User-defined timestamps. These are here for convenience. In many
     * cases, the context one wants to associate with raw/encoded frames
     * is a PTS-DTS pair. If only the context pointer were available, users
     * would have to create a separate data structure containing PTS & DTS
     * values for each context. Since this use case is common, these two
     * fields are added to the frame structure. Just like the context
     * pointer, the library just passes them through to the associated
     * encoded frame, and does not actually touch their values. It is also
     * perfectly OK to not use them, and just use the context pointer
     * instead, or vice versa. */
    uint64_t pts, dts;
} BmJpuRawFrame;

/* Decoder structures and functions */
/* Decoder return codes. With the exception of BM_JPU_DEC_RETURN_CODE_OK, these
 * should be considered hard errors, and the decoder should be closed when they
 * are returned. */
typedef enum {
    /* Operation finished successfully. */
    BM_JPU_DEC_RETURN_CODE_OK = 0,
    /* General return code for when an error occurs. This is used as a catch-all
     * for when the other error return codes do not match the error. */
    BM_JPU_DEC_RETURN_CODE_ERROR,
    /* Input parameters were invalid. */
    BM_JPU_DEC_RETURN_CODE_INVALID_PARAMS,
    /* JPU decoder handle is invalid. This is an internal error, and most likely
     * a bug in the library. Please report such errors. */
    BM_JPU_DEC_RETURN_CODE_INVALID_HANDLE,
    /* Framebuffer information is invalid. Typically happens when the BmJpuFramebuffer
     * structures that get passed to bm_jpu_dec_register_framebuffers() contain
     * invalid values. */
    BM_JPU_DEC_RETURN_CODE_INVALID_FRAMEBUFFER,
    /* Registering framebuffers for decoding failed because not enough framebuffers
     * were given to the bm_jpu_dec_register_framebuffers() function. */
    BM_JPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
    /* A stride value (for example one of the stride values of a framebuffer) is invalid. */
    BM_JPU_DEC_RETURN_CODE_INVALID_STRIDE,
    /* A function was called at an inappropriate time (for example, when
     * bm_jpu_dec_register_framebuffers() is called before a single byte of input data
     * was passed to bm_jpu_dec_decode() ). */
    BM_JPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE,
    /* The operation timed out. */
    BM_JPU_DEC_RETURN_CODE_TIMEOUT,
    /* A function that should only be called once for the duration of the decoding
     * session was called again. One example is bm_jpu_dec_register_framebuffers(). */
    BM_JPU_DEC_RETURN_CODE_ALREADY_CALLED,
    /* Allocation memory failure. */
    BM_JPU_DEC_RETURN_ALLOC_MEM_ERROR,
    /* The resoultion of decoded frame is over limited. */
    BM_JPU_DEC_RETURN_OVER_LIMITED,
    /* bs buffer size is illegal. */
    BM_JPU_DEC_RETURN_ILLEGAL_BS_SIZE
} BmJpuDecReturnCodes;

/* Internal decoder structure */
typedef struct _BmJpuDecoder BmJpuDecoder;

/* Structure used together with bm_jpu_dec_new_initial_info_callback() .
 * The values are filled by the decoder. */
typedef struct {
    /* Width of height of frames, in pixels. Note: it is not guaranteed that
     * these values are aligned to a 16-pixel boundary (which is required
     * for JPU framebuffers). These are the width and height of the frame
     * with actual pixel content. It may be a subset of the total frame,
     * in case these sizes need to be aligned. In that case, there are
     * padding columns to the right, and padding rows below the frames. */
    unsigned int frame_width, frame_height;

    /* Caller must register at least this many framebuffers
     * with the decoder. */
    unsigned int min_num_required_framebuffers;

    /* Image format of the decoded frames. */
    BmJpuImageFormat image_format;

    /* Physical framebuffer addresses must be aligned to this value. */
    unsigned int framebuffer_alignment;

    int roiFrameWidth;
    int roiFrameHeight;
} BmJpuDecInitialInfo;

typedef struct {
    BmJpuDecoder *decoder;

    bm_jpu_phys_addr_t bitstream_buffer_addr;
    size_t bitstream_buffer_size;
    unsigned int bitstream_buffer_alignment;

    BmJpuDecInitialInfo initial_info;

    BmJpuFramebuffer *framebuffers;
    bm_jpu_phys_addr_t *framebuffer_addrs;
    size_t framebuffer_size;
    unsigned int num_framebuffers;
    unsigned int num_extra_framebuffers; // TODO: Motion JPEG

    BmJpuFramebufferSizes calculated_sizes;
    BmJpuRawFrame raw_frame;
    int device_index;

    void *opaque;

    int rotationEnable;
    BmJpuRotateAngle rotationAngle;
    int mirrorEnable;
    BmJpuMirrorDirection mirrorDirection;

    bool framebuffer_recycle;
    bool bitstream_from_user;
    bool framebuffer_from_user;

} BmJpuJPEGDecoder;

/* Structure used together with bm_jpu_dec_open() */
typedef struct {
    /* These are necessary with some formats which do not store the width
     * and height in the bitstream. If the format does store them, these
     * values can be set to zero. */
    unsigned int min_frame_width;
    unsigned int min_frame_height;
    unsigned int max_frame_width;
    unsigned int max_frame_height;

    BmJpuColorFormat color_format;  // Note: forward compatibility, not support on 1684 or 1684x
    BmJpuChromaFormat chroma_interleave;

    /* 0: no scaling; n(1-3): scale by 2^n; */
    unsigned int scale_ratio;

    /* The DMA buffer size for bitstream */
    size_t bs_buffer_size;
#ifdef _WIN32
    uint8_t *buffer;
#else
    uint8_t *buffer __attribute__((deprecated));
#endif

    int device_index;

    int rotationEnable;
    BmJpuRotateAngle rotationAngle;
    int mirrorEnable;
    BmJpuMirrorDirection mirrorDirection;

    int roiEnable;
    int roiWidth;
    int roiHeight;
    int roiOffsetX;
    int roiOffsetY;

    bool framebuffer_recycle;
    size_t framebuffer_size;

    bool bitstream_from_user;
    bm_jpu_phys_addr_t bs_buffer_phys_addr;
    bool framebuffer_from_user;
    int framebuffer_num;
    bm_jpu_phys_addr_t *framebuffer_phys_addrs;

    int timeout;
    int timeout_count;
} BmJpuDecOpenParams;

typedef struct {
    /* Width and height of JPU framebuffers are aligned to internal boundaries.
     * The frame consists of the actual image pixels and extra padding pixels.
     * aligned_frame_width / aligned_frame_height specify the full width/height
     * including the padding pixels, and actual_frame_width / actual_frame_height
     * specify the width/height without padding pixels. */
    unsigned int aligned_frame_width, aligned_frame_height;
    unsigned int actual_frame_width, actual_frame_height;

    /* Stride and size of the Y, Cr, and Cb planes. The Cr and Cb planes always
     * have the same stride and size. */
    unsigned int y_stride, cbcr_stride;
    unsigned int y_size, cbcr_size;

    /* Offset from the start of a framebuffer's memory, in bytes. Note that the
     * Cb and Cr offset values are *not* the same, unlike the stride and size ones. */
    unsigned int y_offset, cb_offset, cr_offset;

    /* Framebuffer containing the pixels of the decoded frame. */
    BmJpuFramebuffer *framebuffer;

    /* Image format of the decoded frame. */
    BmJpuImageFormat image_format;

    bool framebuffer_recycle;
    size_t framebuffer_size;
} BmJpuJPEGDecInfo;

/* Convenience function which calculates various sizes out of the given width & height and image format.
 * The results are stored in "calculated_sizes". The given frame width and height will be aligned if
 * they aren't already, and the aligned value will be stored in calculated_sizes. Width & height must be
 * nonzero. The calculated_sizes pointer must also be non-NULL. framebuffer_alignment is an alignment
 * value for the sizes of the Y/U/V planes. 0 or 1 mean no alignment. uses_interlacing is set to 1
 * if interlacing is to be used, 0 otherwise. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_calc_framebuffer_sizes(unsigned int frame_width,
                                   unsigned int frame_height,
                                   unsigned int framebuffer_alignment,
                                   BmJpuImageFormat image_format,
                                   BmJpuFramebufferSizes *calculated_sizes);

/* Returns a human-readable description of the error code.
 * Useful for logging. */
DECL_EXPORT char const * bm_jpu_dec_error_string(BmJpuDecReturnCodes code);

/* These two functions load/unload the decoder. Due to an internal reference
 * counter, it is safe to call these functions more than once. However, the
 * number of unload() calls must match the number of load() calls.
 *
 * The decoder must be loaded before doing anything else with it.
 * Similarly, the decoder must not be unloaded before all decoder activities
 * have been finished. This includes opening/decoding decoder instances. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_dec_load(int device_index);
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_dec_unload(int device_index);

DECL_EXPORT bm_handle_t bm_jpu_dec_get_bm_handle(int device_index);

/* Opens a new JPU JPEG decoder instance.
 *
 * Internally, this function calls bm_jpu_dec_load().
 *
 * If dma_buffer_allocator is NULL, the default decoder allocator is used.
 *
 * num_extra_framebuffers is used for instructing this function to allocate this many
 * more framebuffers. Usually this value is zero, but in certain cases where many
 * JPEGs need to be decoded quickly, or the DMA buffers of decoded frames need to
 * be kept around elsewhere, having more framebuffers available can be helpful.
 * Note though that more framebuffers also means more DMA memory consumption.
 * If unsure, keep this to zero. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_open(BmJpuJPEGDecoder **jpeg_decoder,
                                         BmJpuDecOpenParams *open_params,
                                         unsigned int num_extra_framebuffers);

/* Closes a JPEG decoder instance. Trying to close the same instance multiple times results in undefined behavior. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_close(BmJpuJPEGDecoder *jpeg_decoder);

/* Decodes a JPEG frame.
 *
 * jpeg_data must be set to the memory block that contains the encoded JPEG data,
 * and jpeg_data_size must be set to the size of that block, in bytes. After this
 * call, use the bm_jpu_jpeg_dec_get_info() function to retrieve information about
 * the decoded frame.
 *
 * The JPU decoder only consumes baseline JPEG data. Progressive encoding is not supported. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_decode(BmJpuJPEGDecoder *jpeg_decoder,
                                           uint8_t const *jpeg_data,
                                           size_t const jpeg_data_size,
                                           int timeout,
                                           int timeout_count);

/* Retrieves information about the decoded JPEG frame.
 *
 * The BmJpuJPEGDecInfo's fields will be set to those of the decoded frame. In particular,
 * info's framebuffer pointer will be set to point to the framebuffer containing the
 * decoded frame. Be sure to pass this pointer to bm_jpu_jpeg_dec_frame_finished() once
 * the frame's pixels are no longer needed.
 *
 * Note that the return value of the previous bm_jpu_jpeg_dec_decode() call can be
 * BM_JPU_DEC_RETURN_CODE_OK even though the framebuffer pointer retrieved here is NULL.
 * This is the case when not enough free framebuffers are present. It is recommended to
 * check the return value of the bm_jpu_jpeg_dec_can_decode() function before calling
 * bm_jpu_jpeg_dec_decode(), unless the decoding sequence is simple (like in the example
 * mentioned in the bm_jpu_jpeg_dec_can_decode() description).
 *
 * This function must not be called before bm_jpu_jpeg_dec_decode() , since otherwise,
 * there is no information available (it is read in the decoding step). */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_get_info(BmJpuJPEGDecoder *jpeg_decoder, BmJpuJPEGDecInfo *info);

/* Inform the JPEG decoder that a previously decoded frame is no longer being used.
 *
 * This function must always be called once the user is done with a frame, otherwise
 * the JPU cannot reclaim this ramebuffer, and will eventually run out of internal
 * framebuffers to decode into. */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_frame_finished(BmJpuJPEGDecoder *jpeg_decoder,
                                                   BmJpuFramebuffer *framebuffer);

/* Flush the JPEG decoder. All framebuffers in decoder will be set to free status */
DECL_EXPORT BmJpuDecReturnCodes bm_jpu_jpeg_dec_flush(BmJpuJPEGDecoder *jpeg_decoder);

/* Encoder structures and functions */
/* Encoder return codes. With the exception of BM_JPU_ENC_RETURN_CODE_OK, these
 * should be considered hard errors, and the encoder should be closed when they
 * are returned. */
typedef enum {
    /* Operation finished successfully. */
    BM_JPU_ENC_RETURN_CODE_OK = 0,
    /* General return code for when an error occurs. This is used as a catch-all
     * for when the other error return codes do not match the error. */
    BM_JPU_ENC_RETURN_CODE_ERROR,
    /* Input parameters were invalid. */
    BM_JPU_ENC_RETURN_CODE_INVALID_PARAMS,
    /* JPU encoder handle is invalid. This is an internal error, and most likely
     * a bug in the library. Please report such errors. */
    BM_JPU_ENC_RETURN_CODE_INVALID_HANDLE,
    /* Framebuffer information is invalid. Typically happens when the BmJpuFramebuffer
     * structures that get passed to bm_jpu_enc_register_framebuffers() contain
     * invalid values. */
    BM_JPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER,
    /* Registering framebuffers for encoding failed because not enough framebuffers
     * were given to the bm_jpu_enc_register_framebuffers() function. */
    BM_JPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
    /* A stride value (for example one of the stride values of a framebuffer) is invalid. */
    BM_JPU_ENC_RETURN_CODE_INVALID_STRIDE,
    /* A function was called at an inappropriate time. */
    BM_JPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE,
    /* The operation timed out. */
    BM_JPU_ENC_RETURN_CODE_TIMEOUT,
    /* A function that should only be called once for the duration of the encoding
     * session was called again. One example is bm_jpu_enc_register_framebuffers(). */
    BM_JPU_ENC_RETURN_CODE_ALREADY_CALLED,
    /* Write_output_data() in BmJpuEncParams returned 0. */
    BM_JPU_ENC_RETURN_CODE_WRITE_CALLBACK_FAILED,
    /* Allocation memory failure. */
    BM_JPU_ENC_RETURN_ALLOC_MEM_ERROR,
    /* JPU Enc byte error. */
    BM_JPU_ENC_BYTE_ERROR,
    /* Bitstream buffer full. */
    BM_JPU_ENC_RETURN_BS_BUFFER_FULL
} BmJpuEncReturnCodes;

/* Internal encoder structure */
typedef struct _BmJpuEncoder BmJpuEncoder;

/* Initial encoding information, produced by the encoder. This structure is
 * essential to actually begin encoding, since it contains all of the
 * necessary information to create and register enough framebuffers. */
typedef struct {
    /* Caller must register at least this many framebuffers
     * with the encoder. */
    unsigned int min_num_required_framebuffers;

    /* Physical framebuffer addresses must be aligned to this value. */
    unsigned int framebuffer_alignment;
} BmJpuEncInitialInfo;

typedef struct {
    BmJpuEncoder *encoder;

    bm_jpu_phys_addr_t bitstream_buffer_addr;
    size_t bitstream_buffer_size;
    unsigned int bitstream_buffer_alignment;

    BmJpuEncInitialInfo initial_info;

    unsigned int frame_width, frame_height;

    BmJpuFramebufferSizes calculated_sizes;

    unsigned int quality_factor;

    BmJpuImageFormat image_format;

    int device_index;

    int rotationEnable;
    BmJpuRotateAngle rotationAngle;
    int mirrorEnable;
    BmJpuMirrorDirection mirrorDirection;

    bool bitstream_from_user;

} BmJpuJPEGEncoder;

/* Function pointer used during encoding for acquiring output buffers.
 * See bm_jpu_enc_encode() for details about the encoding process.
 * context is the value of output_buffer_context specified in
 * BmJpuEncParams. size is the size of the block to acquire, in bytes.
 * acquired_handle is an output value; the function can set this to a
 * handle that corresponds to the acquired buffer. For example, in
 * libav/FFmpeg, this handle could be a pointer to an AVBuffer. In
 * GStreamer, this could be a pointer to a GstBuffer. The value of
 * *acquired_handle will later be copied to the acquired_handle value
 * of BmJpuEncodedFrame.
 * The return value is a pointer to a memory-mapped region of the
 * output buffer, or NULL if acquiring failed.
 * If the write_output_data function pointer in the encoder params
 * is non-NULL, this function is not called.
 * This function is only used by bm_jpu_enc_encode(). */
typedef void* (*BmJpuEncAcquireOutputBuffer)(void *context, size_t size, void **acquired_handle);

/* Function pointer used during encoding for notifying that the encoder
 * is done with the output buffer. This is *not* a function for freeing
 * allocated buffers; instead, it makes it possible to release, unmap etc.
 * context is the value of output_buffer_context specified in
 * BmJpuEncParams. acquired_handle equals the value of *acquired_handle in
 * BmJpuEncAcquireOutputBuffer.
 * If the write_output_data function pointer in the encoder params
 * is non-NULL, this function is not called. */
typedef void (*BmJpuEncFinishOutputBuffer)(void *context, void *acquired_handle);

/* Function pointer used during encoding for passing the output encoded data
 * to the user. If this function is not NULL, then BmJpuEncFinishOutputBuffer
 * and BmJpuEncAcquireOutputBuffer function are not called. Instead, this
 * data write function is called whenever the library wants to write output.
 * encoded_frame contains valid pts, dts, and context data which was copied
 * over from the corresponding raw frame.
 * Returns 1 if writing succeeded, 0 otherwise. */
typedef int (*BmJpuWriteOutputData)(void *context, uint8_t const *data, uint32_t size, BmJpuEncodedFrame *encoded_frame);

typedef struct {
    /* Frame width and height of the input frame. These are the actual sizes;
     * they will be aligned internally if necessary. These sizes must not be
     * zero. */
    unsigned int frame_width, frame_height;

    /* Quality factor for JPEG encoding. 1 = best compression, 100 = best quality.
     * This is the exact same quality factor as used by libjpeg. */
    unsigned int quality_factor;

    /* Image format of the input frame. */
    BmJpuImageFormat image_format;

    /* Functions for acquiring and finishing output buffers. See the
     * typedef documentations for details about how
     * these functions should behave. */
    BmJpuEncAcquireOutputBuffer acquire_output_buffer;
    BmJpuEncFinishOutputBuffer finish_output_buffer;

    /* Function for directly passing the output data to the user
     * without copying it first.
     * Using this function will inhibit calls to acquire_output_buffer
     * and finish_output_buffer. */
    BmJpuWriteOutputData write_output_data;

    /* User supplied value that will be passed to the functions:
     * acquire_output_buffer, finish_output_buffer, write_output_data */
    void *output_buffer_context;

    int rotationEnable;
    BmJpuRotateAngle rotationAngle;
    int mirrorEnable;
    BmJpuMirrorDirection mirrorDirection;

    /* Identify the output data is in device memory or system memory */
    int bs_in_device;

    int timeout;
    int timeout_count;

    /* Optional: User supplied device memory for following encode,
     * will replace the bitstream buffer in encoder */
    bm_jpu_phys_addr_t bs_buffer_phys_addr;
    int bs_buffer_size;
} BmJpuJPEGEncParams;

/* Returns a human-readable description of the error code.
 * Useful for logging. */
DECL_EXPORT char const * bm_jpu_enc_error_string(BmJpuEncReturnCodes code);

/* These two functions load/unload the encoder. Due to an internal reference
 * counter, it is safe to call these functions more than once. However, the
 * number of unload() calls must match the number of load() calls.
 *
 * The encoder must be loaded before doing anything else with it.
 * Similarly, the encoder must not be unloaded before all encoder activities
 * have been finished. This includes opening/decoding encoder instances. */
DECL_EXPORT BmJpuEncReturnCodes bm_jpu_enc_load(int device_index);
DECL_EXPORT BmJpuEncReturnCodes bm_jpu_enc_unload(int device_index);

DECL_EXPORT bm_handle_t bm_jpu_enc_get_bm_handle(int device_index);

/* Opens a new JPU JPEG encoder instance.
 *
 * Internally, this function calls bm_jpu_enc_load().
 *
 * If dma_buffer_allocator is NULL, the default encoder allocator is used. */
DECL_EXPORT BmJpuEncReturnCodes bm_jpu_jpeg_enc_open(BmJpuJPEGEncoder **jpeg_encoder,
                                         bm_jpu_phys_addr_t bs_buffer_phys_addr,
                                         int bs_buffer_size,
                                         int device_index);

/* Closes a JPEG encoder instance. Trying to close the same instance multiple times results in undefined behavior. */
DECL_EXPORT BmJpuEncReturnCodes bm_jpu_jpeg_enc_close(BmJpuJPEGEncoder *jpeg_encoder);

/* Encodes a raw input frame.
 *
 * params must be filled with valid values; frame width and height must not be zero.
 * framebuffer contains the raw input pixels to encode. Its stride and offset values
 * must be valid, and its dma_buffer pointer must point to a DMA buffer that contains
 * the pixel data.
 *
 * During encoding, the encoder will call params->acquire_output_buffer() to acquire
 * an output buffer and put encoded JPEG data into. Once encoding is done, the
 * params->finish_output_buffer() function is called. This is *not* to be confused with
 * a memory deallocation function; it is instead typically used to notify the caller
 * that the encoder won't touch the acquired buffer's contents anymore. It is guaranteed
 * that finish_output_buffer() is called if acquire_output_buffer() was called earlier.
 *
 * If acquired_handle is non-NULL, then the poiner it refers to will be set to the handle
 * produced by acquire_output_buffer(), even if bm_jpu_jpeg_enc_encode() exits with an
 * error (unless said error occurred *before* the acquire_output_buffer() call, in which
 * case *acquired_handle will be set to NULL). If output_buffer_size is non-NULL, the
 * size value it points to will be set to the number of bytes of the encoded JPEG data.
 *
 * The JPU encoder only produces baseline JPEG data. Progressive encoding is not supported. */
DECL_EXPORT BmJpuEncReturnCodes bm_jpu_jpeg_enc_encode(BmJpuJPEGEncoder *jpeg_encoder,
                                           BmJpuFramebuffer const *framebuffer,
                                           BmJpuJPEGEncParams const *params,
                                           void **acquired_handle,
                                           size_t *output_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* __BM_JPEG_INTERFACE_H__ */
