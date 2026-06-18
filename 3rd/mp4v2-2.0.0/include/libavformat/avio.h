/*
 * copyright (c) 2001 Fabrice Bellard
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
#ifndef AVFORMAT_AVIO_H
#define AVFORMAT_AVIO_H

/**
 * @file
 * @ingroup lavf_io
 * Buffered I/O operations
 */

#include <stdint.h>

#include "libavutil/common.h"

#define AVIO_SEEKABLE_NORMAL 0x0001 /**< Seeking works like for a local file */
extern "C"{
typedef struct avio_buffer{
	unsigned char buf[1024];
	int pos;
}avio_buffer;

void avio_w8(struct avio_buffer *s, uint8_t data);
void avio_wb32(struct avio_buffer *s, uint32_t val);
void avio_wb16(struct avio_buffer *s, uint16_t val);
void avio_write(struct avio_buffer *pab, const unsigned char *buf, int size);



#define avio32(value)	((((uint8_t)(value&0xff) << 24))|	\
							(((uint8_t)((value>>8)&0xff) << 16))|	\
							(((uint8_t)((value>>16)&0xff) << 8))|	\
							(((uint8_t)((value>>24)&0xff) << 0))	\
						)


#define avio16(value)	((((uint8_t)(value&0xff) << 8))|	\
							(((uint8_t)((value>>8)&0xff) << 0))	\
						)

/**
 * Bytestream IO Context.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 * sizeof(AVIOContext) must not be used outside libav*.
 *
 * @note None of the function pointers in AVIOContext should be called
 *       directly, they should only be set by the client application
 *       when implementing custom I/O. Normally these are set to the
 *       function pointers specified in avio_alloc_context()
 */
typedef struct AVIOContext {
    /**
     * A class for private options.
     *
     * If this AVIOContext is created by avio_open2(), av_class is set and
     * passes the options down to protocols.
     *
     * If this AVIOContext is manually allocated, then av_class may be set by
     * the caller.
     *
     * warning -- this field can be NULL, be sure to not pass this AVIOContext
     * to any av_opt_* functions in that case.
     */
    //const AVClass *av_class;//cwm
    unsigned char *buffer;  /**< Start of the buffer. */
    int buffer_size;        /**< Maximum buffer size */
    unsigned char *buf_ptr; /**< Current position in the buffer *///cwm ����д�ļ���˵��write_index�����ڶ��ļ���˵��read_index
    unsigned char *buf_end; /**< End of the data, may be less than
                                 buffer+buffer_size if the read function returned
                                 less data than requested, e.g. for streams where
                                 no more data has been received yet. *///cwm ����д�ļ���˵��read_index�����ڶ��ļ���˵��write_index

	avio_buffer *pAvioBuf;
} AVIOContext;

// typedef unsigned char 		uint8_t;
// typedef unsigned short 		uint16_t;
// typedef unsigned int 		uint32_t;
// typedef unsigned long long 	uint64_t;

// typedef signed char 		int8_t;
// typedef signed short 		int16_t;
// typedef signed int 			int32_t;
// typedef signed long long 	int64_t;

#define AVERROR_INVALIDDATA		(-1)
#define FF_MEMORY_POISON 		0x2a
#define CONFIG_FTRAPV			0
#define AV_INPUT_BUFFER_PADDING_SIZE	32

#if 1
#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.
#else
/* Some platforms have E* and errno already negated. */
#define AVERROR(e) (e)
#define AVUNERROR(e) (e)
#endif
}

#endif /* AVFORMAT_AVIO_H */
