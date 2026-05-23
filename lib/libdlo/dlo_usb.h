/** @file dlo_usb.h
 *
 *  @brief Header file for the USB-specific connectivity functions.
 *
 *  This file defines the API between the libdlo.c and the USB driver implementation. This
 *  example implementation uses libusb but it should be simple to replace dlo_usb.c with
 *  some alternative implementation.
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

#ifndef DLO_USB_H
#define DLO_USB_H        /**< Avoid multiple inclusion. */

#include "dlo_structs.h"


/** Return the meaning of the last USB-related error as a human-readable string.
 *
 *  @return  Pointer to error message string (zero-terminated).
 */
extern char * dlo_usb_strerror(void);


/** Initialise the USB communications routines.
 *
 *  @param  flags  Initialisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_init(const dlo_init_t flags);


/** Finalisation call for USB communications routines.
 *
 *  @param  flags  Finalisation flags word (unused flags ignored).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_final(const dlo_final_t flags);


/** Update or create nodes on the device list for any DisplayLink devices found.
 *
 *  @param  init  Is this the first call to the enumeration function?
 *
 *  @return  Return code, zero for no error.
 *
 *  This call will look for any DisplayLink devices which use the particular connection
 *  implementation and either add nodes onto the device list or update nodes if they are
 *  already present.
 *
 *  Once enumeration is complete for all connection types, any nodes on the list which
 *  haven't just been added or updated are removed (as the correponding device can no
 *  longer be found).
 */
extern dlo_retcode_t dlo_usb_enumerate(const bool init);


/** Open a connection to the specified device.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_open(dlo_device_t * const dev);


/** Close the connection with a specified device.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_close(dlo_device_t * const dev);


/** Select the input channel in the specified device.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  buf   Pointer to the buffer containing the channel information.
 *  @param  size  Size of the buffer (bytes).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_chan_sel(const dlo_device_t * const dev, const char * const buf, const size_t size);


/** Switch to the default input channel in the specified device.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_std_chan(const dlo_device_t * const dev);


/** Flush the command buffer contents to the specified device.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_write(dlo_device_t * const dev);


/** Write the contents of a specified command buffer to the specified device.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  buf   Pointer to the buffer containing commands to write.
 *  @param  size  Size of the buffer (bytes).
 *
 *  @return  Return code, zero for no error.
 */
extern dlo_retcode_t dlo_usb_write_buf(dlo_device_t * const dev, char * buf, size_t size);


#endif
