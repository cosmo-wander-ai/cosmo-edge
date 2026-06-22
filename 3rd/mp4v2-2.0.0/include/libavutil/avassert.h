/*
 * copyright (c) 2010 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * simple assert() macros that are a bit more flexible than ISO C assert().
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVUTIL_AVASSERT_H
#define AVUTIL_AVASSERT_H

#include <stdlib.h>
#include <assert.h>
//#include "avutil.h"//cwm_add
//#include "log.h"//cwm_add

/**
 * assert() equivalent, that is always enabled.
 */
#define av_assert0(cond) do {                                           \
	assert(cond);														\
} while (0)


/**
 * assert() equivalent, that does not lie in speed critical code.
 * These asserts() thus can be enabled without fearing speedloss.
 */
#define av_assert1(cond) av_assert0(cond)


/**
 * assert() equivalent, that does lie in speed critical code.
 */
#define av_assert2(cond) av_assert0(cond)

#endif /* AVUTIL_AVASSERT_H */
