/** @file dlo_base.h
 *
 *  @brief Global header file for the core functions of libdlo.
 *
 *  This file defines anything which is not a part of the API that libdlo publishes
 *  up to software using the library (in libdlo.h). It is intended that any functions,
 *  variables and structures defined herein are only for internal use by the libdlo
 *  sources.
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

#ifndef DLO_BASE_H
#define DLO_BASE_H        /**< Avoid multiple inclusion. */

#include "dlo_structs.h"


/** Claim and initialise a new device structure.
 *
 *  @param  type    Device type.
 *  @param  serial  Pointer to serial number string for the device (assumed to be unique to each device).
 *
 *  @return  Pointer to new @a dlo_device_t structure or NULL if failed.
 */
extern dlo_device_t *dlo_new_device(const dlo_devtype_t type, const char * const serial);


/** Given a unique ID for a device, see if we have it in our device list @a dev_list.
 *
 *  @param  serial  Pointer to serial number string for the device (assumed to be unique to each device).
 *
 *  @return  Pointer to @a dlo_device_t structure or NULL if not in @a dev_list.
 *
 *  A side-effect of this call is to update the @a dev->check variable to the current value
 *  of @a check_state so that any devices on @a dev_list which weren't added or looked-up by
 *  the end of an enumeration call can be spotted.
 */
extern dlo_device_t *dlo_device_lookup(const char * const serial);


#endif
