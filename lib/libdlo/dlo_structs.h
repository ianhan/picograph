/** @file dlo_structs.h
 *
 *  @brief This file defines all of the internal structures used by libdlo.
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

#ifndef DLO_STRUCTS_H
#define DLO_STRUCTS_H        /**< Avoid multiple inclusion. */

#include "libdlo.h"
#include "dlo_data.h"


/** Structure holding all of the information specific to a particular device.
 */
typedef struct dlo_device_s dlo_device_t;

/** Established timing information, derived from EDID.
 */
typedef struct est_timing_s
{
  uint16_t width;        /**< Width of mode (pixels). */
  uint16_t height;       /**< Height of mode (pixels). */
  uint8_t  refresh;      /**< Mode refresh rate (Hz). */
} est_timing_t;          /**< A struct @a est_timings_s. */


/** Vendor/product information.
 */
typedef struct edid_prod_id_s
{
  uint16_t manuf_name;   /**< ID manufacturer code. */
  uint16_t prod_code;    /**< ID product code. */
  uint32_t serial_num;   /**< ID serial number. */
  uint8_t  manuf_wk;     /**< Week of manufacture. */
  uint8_t  manuf_yr;     /**< Year of manufacture. */
} edid_prod_id_t;        /**< A struct @a edid_prod_id_s. */


/** EDID structure information.
 */
typedef struct edid_struct_vsn_s
{
  uint8_t number;        /**< Version number. */
  uint8_t revision;      /**< Revision number. */
} edid_struct_vsn_t;     /**< A struct @a edid_struct_vsn_s. */


/** Basic dislpay parameters/features.
 */
typedef struct edid_basic_params_s
{
  uint8_t input_def;     /**< Video input definition. */
  uint8_t max_horiz_sz;  /**< Maximum horizontal image size (cm). */
  uint8_t max_vert_sz;   /**< Maximum vertical image size (cm). */
  float   gamma;         /**< Display transfer characteristic (gamma). */
  uint8_t features;      /**< Feature support. */
} edid_basic_params_t;   /**< A struct @a edid_basic_params_s. */


/** Colour characteristics.
 */
typedef struct edid_colours_s
{
  uint16_t red_grn_low;  /**< Red/green low bits. */
  uint16_t blu_wht_low;  /**< Blue/white low bits. */
  uint16_t red_x;        /**< Red-x (bits 9-2). */
  uint16_t red_y;        /**< Red-y (bits 9-2). */
  uint16_t grn_x;        /**< Green-x (bits 9-2). */
  uint16_t grn_y;        /**< Green-y (bits 9-2). */
  uint16_t blu_x;        /**< Blue-x (bits 9-2). */
  uint16_t blu_y;        /**< Blue-y (bits 9-2). */
  uint16_t wht_x;        /**< White-x (bits 9-2). */
  uint16_t wht_y;        /**< White-y (bits 9-2). */
} edid_colours_t;        /**< A struct @a edid_colours_s. */


/** Established timings.
 */
typedef struct edid_est_timings_s
{
  uint8_t timings[2];    /**< Bitfields of established timings. */
  uint8_t resvd;         /**< Manufacturer's reserved timings. */
} edid_est_timings_t;    /**< A struct @a edid_est_timings_s. */


/** Standard timing identification.
 */
typedef struct edid_std_timing_s
{
  uint16_t timing_id[8];  /**< Standard timing identification. */
} edid_std_timing_t;      /**< A struct @a edid_std_timing_s. */


/** Standard EDID Detailed Timing Block, in unpacked form.
 ** There are 4 of them (18 bytes in packed form) 
 ** in each 128 byte EDID returned from the monitor.
 ** The first of those 4 is the preferred mode for the monitor.
 **
 ** Only the packed form is called out in the standard. This
 ** unpacked form is just for convenience, since the packed
 ** form scatters low and higher order bits all over.
 **
 ** Units for all items are pixels (horizontal), 
 ** or lines (vertical), unless otherwise called out
 */ 
typedef struct edid_detail_unpacked_s 
{
  uint16_t pixelClock10KHz; /**< in 10kHz units */
  uint16_t hActive;
  uint16_t hBlanking;
  uint16_t vActive;
  uint16_t vBlanking;
  uint16_t hSyncOffset;
  uint16_t hSyncPulseWidth;
  uint16_t vSyncOffset;
  uint16_t vSyncPulseWidth;
  uint16_t hImageSizeMm;    /**< in millimeters */
  uint16_t vImageSizeMm;    /**< in millimeters */
  uint8_t  hBorder;
  uint8_t  vBorder;
  uint8_t  bInterlaced;
  uint8_t  bStereo;
  uint8_t  bSeparateSync;
  uint8_t  bVSyncPositive;
  uint8_t  bHSyncPositive;
  uint8_t  bStereoMode;
} edid_detail_unpacked_t;

/** An EDID extension block.
 */
typedef struct edid_ext_block_s
{
  uint8_t unknown[128];  /**< Contents of block are unknown. */
} edid_ext_block_t;      /**< A struct @a edid_ext_block_s. */

/** Struture holding parsed EDID information
 */ 
typedef struct dlo_edid_s
{
  edid_prod_id_t      product;      /**< Vendor/product information. */
  edid_struct_vsn_t   version;      /**< EDID structure information. */
  edid_basic_params_t basic;        /**< Basic dislpay parameters/features. */
  edid_colours_t      colours;      /**< Colour characteristics. */
  edid_est_timings_t  est_timings;  /**< Established timings. */
  edid_std_timing_t   std_timings;  /**< Standard timing identification. */
  edid_detail_unpacked_t timings[4];/**< Detailed timing descriptions. */
  uint8_t             ext_blocks;   /**< Number of extension blocks. */
} dlo_edid_t;                    


/** A mode number used to index a specific mode from the list defined in dlo_mode_data.c.
 */
typedef uint32_t dlo_modenum_t;


/** Structure used internally by dlo_usb.c (stored as dev->cnct in @a dlo_device_t structure).
 *
 *  This is required to keep track of which USB device a given @a dlo_device_t structure
 *  represents so that our various functions can do their stuff with libusb.
 */
typedef struct dlo_usb_dev_s
{
  uint8_t udev;              /**< TinyUSB device address. */
  uint8_t uhand;             /**< TinyUSB bulk endpoint address once opened. */
} dlo_usb_dev_t;             /**< A struct @a dlo_usb_dev_s. */


/** An internal representation of a viewport within the DisplayLink device.
 *
 *  An area is generated from a viewport and a rectangle within that viewport (which
 *  has no parts lying outside but may cover the complete extent of the viewport). It
 *  has a base address for both the 16 bpp component of a pixel's colour and the 8 bpp
 *  fine detail component. It also requires a stride in the case where the rectangle
 *  didn't fully occupy the horizontal extent of the viewport.
 */
typedef struct dlo_area_s
{
  dlo_view_t view;           /**< Viewport information (normalised to a specific rectangle within a viewport). */
  dlo_ptr_t  base8;          /**< The base address of the 8 bpp fine detail colour information. */
  uint32_t   stride;         /**< The stride (pixels) from one pixel in the area to the one directly below. */
} dlo_area_t;                /**< A struct @a dlo_area_s. */


/** Structure holding all of the information specific to a particular device.
 */
struct dlo_device_s
{
  dlo_device_t  *prev;       /**< Pointer to previous node on device list. */
  dlo_device_t  *next;       /**< Pointer to next node on device list. */
  dlo_devtype_t  type;       /**< Type of DisplayLink device. */
  char          *serial;     /**< Pointer to device serial number string. */
  dlo_edid_t     edid;       /**< Parsed EDID information from device */
  bool           claimed;    /**< Has the device been claimed by someone? */
  bool           check;      /**< Flag is toggled for each enumeration to spot dead nodes in device list. */
  uint32_t       timeout;    /**< Timeout for bulk communications (milliseconds). */
  uint32_t       memory;     /**< Total size of storage in the device (bytes). */
  char          *buffer;     /**< Pointer to the base of the command buffer. */
  char          *bufptr;     /**< Pointer to the first free byte in the command buffer. */
  char          *bufend;     /**< Pointer to the byte after the end byte of the command buffer. */
  dlo_usb_dev_t *cnct;       /**< Private word for connection specific data or structure pointer. */
  dlo_mode_t     mode;       /**< Current display mode information. */
  dlo_ptr_t      base8;      /**< Pointer to the base of the 8bpp segment (if any). */
  bool           low_blank;  /**< The current raster screen mode has reduced blanking. */
  dlo_mode_t     native;     /**< Mode number of the display's native screen mode (if any). */
  dlo_modenum_t  supported[DLO_MODE_DATA_NUM];  /**< Array of supported mode numbers. */
};


#endif
