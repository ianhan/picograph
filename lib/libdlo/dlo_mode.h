/** @file dlo_mode.h
 *
 *  @brief Definitions for the screen mode-related functions.
 *
 *  These functions have been separated-out into a distinct compilation unit so that
 *  it would be possible to build a distribution of the DisplayLink library which
 *  inludes the object(s) associated with this unit as pre-built binaries (i.e.
 *  without the dlo_mode.c source file included).
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

#ifndef DLO_MODE_H
#define DLO_MODE_H        /**< Avoid multiple inclusion. */

#include "dlo_structs.h"


/** Size of an EDID structure, as read from a device (bytes).
 */
#define EDID_STRUCT_SZ (128)


/** An undefined mode number. Used to indicate a failed look-up or an unknown mode.
 */
#define DLO_INVALID_MODE ((dlo_modenum_t)-1)


/** Initialise the screen mode handling functions.
 *
 *  @param  flags  Initialisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_mode_init(const dlo_init_t flags);


/** Finalisation call for the screen mode handling functions.
 *
 *  @param  flags  Finalisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_mode_final(const dlo_final_t flags);


/** Return a block of mode information for a given mode number.
 *
 *  @param  num  Mode number for best match to the supplied bitmap.
 *
 *  @return  Pointer to mode information block (in static workspace), or NULL if error.
 */
extern dlo_mode_t *dlo_mode_from_number(const dlo_modenum_t num);


/** Select a display mode given a set of parameters.
 *
 *  @param  dev      Pointer to @a dlo_device_t structure.
 *  @param  width    Width of desired display (pixels).
 *  @param  height   Hieght of desired display (pixels) - zero to select first available.
 *  @param  refresh  Desired refresh rate (Hz) - zero to select first available.
 *  @param  bpp      Colour depth (bits per pixel) - zero to select first available.
 *
 *  @return  Return code, zero for no error.
 *
 *  Will find the best match it can from the VESA timings, given the parameters specified.
 *  If no matching mode can be found, an error will be returned and the device's screen
 *  mode will not be changed.
 */
extern dlo_modenum_t dlo_mode_lookup(dlo_device_t * const dev, const uint16_t width, const uint16_t height, const uint8_t refresh, uint8_t bpp);


/** Perform a mode change into the specified mode number.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  desc  Pointer to mode description structure.
 *  @param  mode  Mode number for best match to the mode descriptor (or @a DLO_INVALID_MODE if not known).
 *
 *  @return  Return code, zero for no error.
 *
 *  This call will change the screen mode into the specified mode number (if one was given)
 *  or look for one which matches the specified mode description. Chaging mode does not imply
 *  clearing the screen!
 *
 *  Note: this call will cause any buffered commands to be sent to the device.
 */
extern dlo_retcode_t dlo_mode_change(dlo_device_t * const dev, const dlo_mode_t * const desc, dlo_modenum_t mode);

/** Set the mode of a DisplayLink chip 
 *  to the monitor's preferred mode,
 *  using the EDID detailed timing descriptor
 *  Returns error if EDID detailed timing is not available
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  base  Base address of view in framebuffer. Can be zero.
 *
 *  @return  Return code, zero for no error.
 *
 *  Since this data originates from EDID, it is assumed that the 
 *  monitor will support the mode. So the only checks will be if
 *  the DisplayLink chip supports it, too.
 * 
 *  Note: Chaging mode does not imply clearing the screen.
 *  Note: this call will cause any buffered commands to be sent to the device.
 */
extern dlo_retcode_t dlo_mode_set_default(dlo_device_t * const dev, uint32_t base);

/** Parse the EDID structure read from a display device and build a list of supported modes.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  ptr   Pointer to EDID structure.
 *  @param  size  Size of EDID structure (bytes).
 *
 *  @return  Return code, zero for no error.
 *
 *  This call updates the @a dlo_device_t structure by constructing a list of supported
 *  modes. Any modes which were not supplied in the EDID structure will be refused for
 *  that device.
 */
extern dlo_retcode_t dlo_mode_parse_edid(dlo_device_t * const dev, const uint8_t * const ptr, const size_t size);


/** Reset the supported modes array for a device to include all of the default VESA mode timings.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 */
extern void use_default_modes(dlo_device_t * const dev);


#endif
