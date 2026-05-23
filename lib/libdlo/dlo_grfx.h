/** @file dlo_grfx.h
 *
 *  @brief This file defines the external API of the DisplayLink libdlo graphics functions.
 *
 *  This API is used by clients of the Bitmap Manager (e.g. other parts of libdlo) to access its
 *  features. It is intended that all implementation details are abstracted-away by this interface.
 *
 *  DisplayLink Open Source Software (libdlo)
 *  Copyright (C) 2009, DisplayLink
 *  www.displaylink.com
 *
 *  This library is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Library General Public License as published by the Free
 *  Software Foundation; LGPL version 2, dated June 1991.
 *
 *  This library is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU Library General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef DLO_GRFX_H
#define DLO_GRFX_H        /**< Avoid multiple inclusion. */

#include "dlo_structs.h"


#define WRITE_RAW8   "\xAF\x60"  /**< 8 bit raw write command. */
#define WRITE_RL8    "\xAF\x61"  /**< 8 bit run length write command. */
#define WRITE_COPY8  "\xAF\x62"  /**< 8 bit copy command. */
#define WRITE_RAW16  "\xAF\x68"  /**< 16 bit raw write command. */
#define WRITE_RL16   "\xAF\x69"  /**< 16 bit run length write command. */
#define WRITE_COPY16 "\xAF\x6A"  /**< 16 bit copy command. */


/** Initialise the graphics primitive routines.
 *
 *  @param  flags  Initialisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_grfx_init(const dlo_init_t flags);


/** Finalisation call for graphics primitive routines.
 *
 *  @param  flags  Finalisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_grfx_final(const dlo_final_t flags);


/** Plot a filled rectangle into the specified device.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  area  Struct pointer: area within device memory to fill.
 *  @param  col   Colour of filled rectangle.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_grfx_fill_rect(dlo_device_t * const dev, const dlo_area_t * const area, const dlo_col32_t col);


/** Copy a rectangular area within the device from one location to another.
 *
 *  @param  dev        Pointer to @a dlo_device_t structure.
 *  @param  src_area   Struct pointer: area within device memory to copy from.
 *  @param  dest_area  Struct pointer: area within device memory to copy to.
 *  @param  overlap    true if rectangles overlap or false if not.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_grfx_copy_rect(dlo_device_t * const dev, const dlo_area_t * const src_area, const dlo_area_t * const dest_area, const bool overlap);


/** Copy (and translate pixel formats) a rectangular area from host memory into the device.
 *
 *  @param  dev    Pointer to @a dlo_device_t structure.
 *  @param  flags  Flags word indicating special behaviour (unused flags should be zero).
 *  @param  fbuf   Struct pointer: area within host memory to copy from.
 *  @param  area   Struct pointer: area within device memory to copy into.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_grfx_copy_host_bmp(dlo_device_t * const dev, const dlo_bmpflags_t flags, const dlo_fbuf_t const *fbuf, const dlo_area_t * const area);


#endif
