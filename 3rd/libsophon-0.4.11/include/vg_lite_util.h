/*!
 @header VGLite Utilities
 Some common utilities that can be used by programs to ease programming with the VGLite API.

 @copyright Vivante Corporation
 */

#ifndef _vg_lite_util_h_
#define _vg_lite_util_h_

#include "vg_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 @abstract Load a PNG file into a buffer.

 @discussion
 Using the PNG library, a PNG file will be read into memory and a <code>vg_lite_buffer_t</code> structure will be filled in with 
 the information about this PNG file.
 
 Normal kernel allocation will happen for the PNG file bits, so the buffer can later be freed by the {@link vg_lite_free}
 function when it is no longer required.
 
 @param buffer
 A pointer to a <code>vg_lite_buffer_t</code> structure that will be filled in on success.
 
 @param name
 The name of the PNG file.

 @result
 A boolean value indicating success (1) or failure (0).
 */
int vg_lite_load_png(vg_lite_buffer_t * buffer, const char * name);

/*!
 @abstract Save a buffer to a PNG file.

 @discussion
 Using the PNG library, the specified buffer will be written into a PNG.

 @param name
 The name of the PNG file.

 @param buffer
 A pointer to a <code>vg_lite_buffer_t</code> structure that holds the image that needs to written.

 @result
 A boolean value indicating success (1) or failure (0).
 */
int vg_lite_save_png(const char * name, vg_lite_buffer_t * buffer);

/*!
 @abstract Get a buffer pointing to the frame buffer if any.
 
 @discussion
 On most devices their will be a frame buffer attachment. This function wraps the complexities of using the frame buffer device
 as an output. The buffer returned holds the information about the frame buffer. When the frame buffer is no longer needed,
 {@link vg_lite_fb_close} should be called with the buffer returned by this function.
 
 If the device doesn't support a frame buffer, <code>NULL</code> will be returned.
 
 @result
 Returns a pointer to a <code>vg_lite_buffer_t</code> structure which can be used as a normal buffer on success. On failure
 <code>NULL</code> will be returned.
 */
vg_lite_buffer_t * vg_lite_fb_open(void);

/*!
 @abstract Get a buffer pointing to the frame buffer if any.

 @discussion
 On most devices their will be a frame buffer attachment. This function wraps the complexities of using the frame buffer device
 as an output. The buffer returned holds the information about the frame buffer. When the frame buffer is no longer needed,
 {@link vg_lite_fb_close} should be called with the buffer returned by this function.

 If the device doesn't support a frame buffer, <code>NULL</code> will be returned.

 @result
 Returns a pointer to a <code>vg_lite_buffer_t</code> structure which can be used as a normal buffer on success. On failure
 <code>NULL</code> will be returned.
 */
void vg_lite_fb_close(vg_lite_buffer_t * buffer);

/*!
 @abstract Read raw files in yuv format.

 @discussion
 Save raw files in yuv format to the buffer memory.Before using the function,need to set buffer->height,buffer->width,buffer->format.

 @result
 Returns an int value indicating success (0) or failure (-1).
 */
int vg_lite_load_raw_yuv(vg_lite_buffer_t * buffer, const char * name);

/*!
 @abstract Convert YV12 format files into YV24 format files.

 @discussion
 Input YV12 format buffer to get YV24 format buffer.
 
 @param buffer1
 A pointer to YV24 format file memory.
 
 @param buffer2
 A pointer to YV12 format file memory.
 
 @result
 Returns an int value indicating success (0) or failure (-1).
 */
int vg_lite_yv12toyv24(vg_lite_buffer_t * buffer1, vg_lite_buffer_t * buffer2);

/*!
 @abstract Convert YV12 format files into YV16 format files.

 @discussion
 Input YV12 format buffer to get YV16 format buffer.
 
 @param buffer1
 A pointer to YV16 format file memory.
 
 @param buffer2
 A pointer to YV12 format file memory.
 
 @result
 Returns an int value indicating success (0) or failure (-1).
 */
int vg_lite_yv12toyv16(vg_lite_buffer_t * buffer1, vg_lite_buffer_t * buffer2);
int vg_lite_load_raw(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_load_raw_byline(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_save_raw(const char *name, vg_lite_buffer_t *buffer);
int vg_lite_save_raw_byline(const char *name, vg_lite_buffer_t *buffer);
int vg_lite_load_pkm(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_load_pkm_info_to_buffer(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_load_dev_info_to_buffer(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_load_decnano_compressd_data(vg_lite_buffer_t * buffer, const char * name);
int vg_lite_save_decnano_compressd_data(const char *name, vg_lite_buffer_t *buffer);
int vg_lite_save_decnano_2_0_compressd_data(const char* name, vg_lite_buffer_t* buffer);

#ifdef __cplusplus
}
#endif
#endif /* _vg_lite_util_h_ */
