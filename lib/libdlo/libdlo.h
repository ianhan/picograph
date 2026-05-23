/** @file libdlo.h
 *
 *  @brief This file defines the high-level API for the DisplayLink libdlo library.
 *
 *  It is a simplified abstraction layer to allow programs to talk to DisplayLink
 *  compatible devices.
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

/** @mainpage DisplayLink Open Source Software (libdlo)
 *  @anchor dlppdoxygenmain
 *
 *  This documentation covers the DisplayLink Open Source Software (libdlo) library (libdlo).
 *  It covers the following items:
 *
 *  @li File by file information
 *  @li Function by function information
 *  @li Global variables, defines and structures
 *
 *  <hr>
 *
 *  <b>Overview</b>
 *
 *  <p>
 *  This library is stuctured as illustrated below, in order to help clarify the major
 *  functional areas and simplify the task of porting or reimplementing these functions
 *  to other platforms. At the very least, it should serve as a reference implementation
 *  which can be replaced in its entirety.
 *  </p>
 *
 *  @image html libdlo.gif "libdlo internal structure"
 *
 *  <p>
 *  All communications with libdlo are through the API published in libdlo.h. This API
 *  allows third party software to find DisplayLink devices, connect to them, perform
 *  graphical operations and then disconnect as required. There are a number of graphical
 *  primitives available, including rectangle plotting and copying (rectangular) areas of
 *  screen.
 *  </p>
 *
 *  <hr>
 *  <b>Implementation Notes: Functions</b>
 *
 *  <p>
 *  There are primarily three types of function used within all of the libdlo sources:
 *  </p>
 *
 *  @li A void function or non-error returning function
 *  @li A pointer-returning function
 *  @li A function returning a return code
 *
 *  <p>
 *  Void and non-error returning functions are used in situations where there is no
 *  possibility or an error being raised or where there would be no value in returning it.
 *  Examples might include functions which simply perform a computation or those which
 *  dispose of a structure and are expected to fail silently.
 *  </p>
 *
 *  <p>
 *  Pointer returning functions will conventionally return a NULL pointer if there is
 *  an error. Normally the error will be due to memory allocation failure (out of
 *  memory) but this is not always the case. As a NULL pointer it not very informative
 *  about the nature of the failure, the use of pointer-returning functions within
 *  libdlo has been kept to a minimum.
 *  </p>
 *
 *  <p>
 *  The majority of functions return an code of type @a dlo_retcode_t to indicate
 *  success or the nature of failure. All return codes have an associated textual string
 *  so they can be decoded in a human-readable format by calling @c dlo_strerror().
 *  </p>
 *
 *  <hr>
 *  <b>Implementation Notes: Error Handling</b>
 *
 *  <p>
 *  In order to keep the handling of errors consistent, there are a number of handy
 *  macros defined in dlo_defs.h. There are a few which collect any non-zero return
 *  value from a given function call and return it from the calling function (there
 *  is also a version of this call which checks for NULL pointer returns and converts
 *  it into an memory allocation failure return code). These macros are:
 *  </p>
 *
 *  @li @c ERR()
 *  @li @c NERR()
 *  @li @c UERR()
 *
 *  <p>
 *  A second set of macros are defined which, instead of returning a return code from
 *  the calling function, they store the error in a local variable "err" and jump to
 *  a label called "error". This allows the function to perform whatever tidy-up
 *  operations it needs to before returning "err". These macros are:
 *  </p>
 *
 *  @li @c ERR_GOTO()
 *  @li @c NERR_GOTO()
 *  @li @c UERR_GOTO()
 *
 *  <p>
 *  Both of these sets of macros are used throughout the libdlo sources in order to
 *  make the handling of errors consistent.
 *  </p>
 *
 *  <hr>
 *  Copyright &copy; 2008, DisplayLink
 *  All rights reserved.
 */

#ifndef DLO_LIBDLO_H
#define DLO_LIBDLO_H        /**< Avoid multiple inclusion. */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if 0
#define dlo_malloc  my_malloc
#define dlo_realloc my_realloc
#define dlo_free    my_free

/** Alternative malloc routine.
 *
 *  @param  size  As per @c malloc().
 *
 *  @return  As per @c malloc().
 */
extern void *my_malloc(size_t size);

/** Alternative realloc routine.
 *
 *  @param  blk   As per @c realloc().
 *  @param  size  As per @c realloc().
 *
 *  @return  As per @c realloc().
 */
extern void *my_realloc(void *blk, size_t size);

/** Alternative free routine.
 *
 *  @param  blk   As per @c free().
 */
extern void my_free(void *blk);

/** Return total size of allocated blocks.
 *
 *  @return  Size of all current allocations (bytes).
 */
extern uint32_t my_used(void);
#endif


/** Allow the use of the standard C library @a malloc() function to be overridden.
 */
#ifndef dlo_malloc
#define dlo_malloc malloc
#endif

/** Allow the use of the standard C library @a realloc() function to be overridden.
 */
#ifndef dlo_realloc
#define dlo_realloc realloc
#endif

/** Allow the use of the standard C library @a free() function to be overridden.
 */
#ifndef dlo_free
#define dlo_free free
#endif

/** Allow the use of the standard C library @a memset() function to be overridden.
 */
#ifndef dlo_memset
#define dlo_memset (void) memset
#endif

/** Allow the use of the standard C library @a memcpy() function to be overridden.
 */
#ifndef dlo_memcpy
#define dlo_memcpy (void) memcpy
#endif

/** Allow the use of the standard C library @a memmove() function to be overridden.
 */
#ifndef dlo_memmove
#define dlo_memmove (void) memmove
#endif


/** An opaque device handle. */
typedef void *dlo_dev_t;


/** Return codes used within the libdlo sources. Note: libdlo will never return top-bit-set return
 *  codes so these can safely be allocated within your own programs for your own purposes.
 *
 *  Return code 0x00000000 represents a successful return without error or warning.
 *  Return codes in the range 0x00000001..0x0FFFFFFF represent an error condition.
 *  Return codes in the range 0x10000000..0x7FFFFFFF represent a warning.
 *  Return codes in the range 0x80000000..0xFFFFFFFF are free for caller allocation.
 */
typedef enum
{
  /* Success... */
  dlo_ok = 0u,               /**< Successful. */
  /* Errors... */
  dlo_err_memory = 1u,       /**< A memory claim operation failed; out of memory. */
  dlo_err_bad_area,          /**< An invalid area was specified (e.g. of zero width or height). */
  dlo_err_bad_col,           /**< Unsupported colour depth. */
  dlo_err_bad_device,        /**< Unknown device - has been disconnected or powered down. */
  dlo_err_bad_fbuf,          /**< Null pointer passed as local bitmap data. */
  dlo_err_bad_fmt,           /**< Unsupported bitmap pixel format. */
  dlo_err_bad_mode,          /**< Call to @c set_mode() failed due to unsupported mode parameters. */
  dlo_err_bad_view,          /**< Invalid viewport specified (is screen mode set up?). */
  dlo_err_big_scrape,        /**< Bitmap is too wide for copy buffer.*/
  dlo_err_buf_full,          /**< Command buffer is full. */
  dlo_err_claimed,           /**< Device cannot be claimed - it's already been claimed. */
  dlo_err_edid_fail,         /**< EDID communication with monitor failed. */
  dlo_err_iic_op,            /**< An IIC operation with the device failed. */
  dlo_err_not_root,          /**< Executable should be run as root (e.g. using 'su root' or 'sudo'). */
  dlo_err_open,              /**< Attempt to open a connection to the device failed. */
  dlo_err_overlap,           /**< Source and destination viewports cannot overlap (unless the same). */
  dlo_err_reenum,            /**< Reenumeration required before device can be claimed. */
  dlo_err_unclaimed,         /**< Device cannot be written to: unclaimed. */
  dlo_err_unsupported,       /**< Requested feature is not supported. */
  dlo_err_usb,               /**< A USB-related error: call @c dlo_usb_strerror() for further info. */
  /* Warnings... */
  dlo_warn_dl160_mode = 0x10000000u, /**< This screen mode may not display correctly on DL120 devices. */
  dlo_warn_no_edid_detailed_timing,  /**< EDID descriptor not detailed timing */
  /* User return codes... */
  dlo_user_example = 0x80000000      /**< Return codes 0x80000000 to 0xFFFFFFFF are free for user allocation. */
} dlo_retcode_t;             /**< Return codes. Used to indicate the success or otherwise of a call to the library. */


/** A 32 bits per pixel colour number (0x00bbggrr, little endian). Note: most significant byte is undefined. */
typedef uint32_t dlo_col32_t;


/** Return a 32 bpp colour number when given the three RGB components. */
#define DLO_RGB(red,grn,blu) (dlo_col32_t)(((red) & 0xFF) | (((grn) & 0xFF) << 8) | (((blu) & 0xFF) << 16))

/** Set the red component (0..255) of a 32 bpp colour. */
#define DLO_RGB_SETRED(col,red) (dlo_col32_t)(((col) & ~0xFF) | ((red) & 0xFF))

/** Set the green component (0..255) of a 32 bpp colour. */
#define DLO_RGB_SETGRN(col,grn) (dlo_col32_t)(((col) & ~0xFF00) | (((grn) & 0xFF) << 8))

/** Set the blue component (0..255) of a 32 bpp colour. */
#define DLO_RGB_SETBLU(col,blu) (dlo_col32_t)(((col) & ~0xFF0000) | (((blu) & 0xFF) << 16))

/** Read the red component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETRED(col) (uint8_t)((col) & 0xFF)

/** Read the green component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETGRN(col) (uint8_t)(((col) >> 8) & 0xFF)

/** Read the blue component (0..255) of a 32 bpp colour. */
#define DLO_RGB_GETBLU(col) (uint8_t)(((col) >> 16) & 0xFF)


/** Types of DisplayLink device. */
typedef enum
{
  dlo_dev_unknown = 0,       /**< Unknown device type. */
  dlo_dev_base    = 0xB,     /**< Base platform. */
  dlo_dev_alex    = 0xF,     /**< Alex chipset. */
  dlo_dev_ollie   = 0xF1     /**< Ollie chipset. */
} dlo_devtype_t;             /**< A struct @a dlo_devtype_s. */


/** A structure containing information about a specific device. */
typedef struct dlo_devinfo_s
{
  dlo_dev_t     uid;         /**< Unique ID for referencing the device. */
  char         *serial;      /**< Pointer to serial number string for device. */
  dlo_devtype_t type;        /**< Device type. */
  bool          claimed;     /**< Flag indicating whether someone has claimed the device. */
} dlo_devinfo_t;             /**< A struct @a dlo_devinfo_s. */


/** A list of devices, as returned by the enumeration call. */
typedef struct dlo_devlist_s dlo_devlist_t;

/** A list of devices, as returned by the enumeration call. */
struct dlo_devlist_s
{
  dlo_devlist_t *next;       /**< Pointer to next node in the device list (or NULL). */
  dlo_devinfo_t  dev;        /**< Device information structure. */
};                           /**< A struct @a dlo_devlist_s. */


/** Flags word for the initialisation function (no flags defined at present). */
typedef struct dlo_init_s
{
  unsigned undef :1;         /**< Undefined flag (placeholder). */
} dlo_init_t;                /**< A struct @a dlo_init_s. */


/** Flags word for the finalisation function (no flags defined at present). */
typedef struct dlo_final_s
{
  unsigned undef :1;         /**< Undefined flag (placeholder). */
} dlo_final_t;               /**< A struct @a dlo_final_s. */


/** Flags word to configure the device connection (no flags defined at present). */
typedef struct dlo_claim_s
{
  unsigned undef :1;         /**< Undefined flag (placeholder). */
} dlo_claim_t;               /**< A struct @a dlo_claim_s. */


/** Flags word to configure the @c dlo_copy_host_bmp() call. */
typedef struct dlo_bmpflags_s
{
  unsigned v_flip :1;        /**< Vertically flip the bitmap during the copy. */
} dlo_bmpflags_t;            /**< A struct @a dlo_bmpflags_s. */


/** Shift (bits) for position of bytes-per-pixel data in pixel format word.
 */
#define DLO_PIXFMT_BYPP_SFT (6u)

/** Bit mask for extracting bytes-per-pixel data from pixel format word.
 */
#define DLO_PIXFMT_BYPP_MSK (0x7 << DLO_PIXFMT_BYPP_SFT)

/** Shift (bits) for position of red/blue swap flag in pixel format word.
 */
#define DLO_PIXFMT_SWP_SFT (9u)

/** Shift (bits) for position of pointer data (most significant bits).
 */
#define DLO_PIXFMT_PTR_SFT (10u)

/** Maximum number of pixel format variations.
 */
#define DLO_PIXFMT_MAX (1u << DLO_PIXFMT_PTR_SFT)

/** Pixel format has one byte per pixel.
 */
#define DLO_PIXFMT_1BYPP (1u << DLO_PIXFMT_BYPP_SFT)

/** Pixel format has two bytes per pixel.
 */
#define DLO_PIXFMT_2BYPP (2u << DLO_PIXFMT_BYPP_SFT)

/** Pixel format has three bytes per pixel.
 */
#define DLO_PIXFMT_3BYPP (3u << DLO_PIXFMT_BYPP_SFT)

/** Pixel format has four bytes per pixel.
 */
#define DLO_PIXFMT_4BYPP (4u << DLO_PIXFMT_BYPP_SFT)

/** Pixel format has red and blue colour components reversed.
 */
#define DLO_PIXFMT_SWP (1u << DLO_PIXFMT_SWP_SFT)

/** Supported source pixel format information (e.g. bits per pixel, colour component order, etc).
 *
 *  Each value in this enum encodes the pixel RGB vs BGR colour component order and
 *  the number of bytes per pixel. There is one function for reading each pixel format
 *  (but the RGB vs BGR is passed in as a flag) and the bytes per pixel is required to
 *  advance the pixel pointer in a framebuffer after a pixel has been read.
 *
 *  Bits 0..5 contain the pixel format code
 *  Bits 6..8 contain the number of bytes per pixel
 *  Bit  9    contains the swap flag for RGB vs BGR
 *  Bits 10.. if non-zero, then the pixel format is actually a pointer to an 8 bit LUT
 *
 *  The pixel format can also simply be a pointer to a 256 entry LUT of dlo_col32_t
 *  colours in the case where the local framebuffer contains 8bpp bitmap data which
 *  uses a palette. Simply cast the LUT pointer to a dlo_pixfmt_t (this assumes that
 *  your LUT isn't stored in the bottom 1KB of the logical address space!). E.g.
 *
 *  static dlo_col32_t mylut[256]; // this is the palette for my 8bpp bitmap
 *  static dlo_pixfmt_t mypixfmt = (dlo_pixfmt_t)mylut;
 */
typedef enum
{
  dlo_pixfmt_bgr323   = 0 | DLO_PIXFMT_1BYPP,                   /**< 8 bit per pixel 2_bbbggrrr. */
  dlo_pixfmt_rgb323   = 0 | DLO_PIXFMT_1BYPP | DLO_PIXFMT_SWP,  /**< 8 bit per pixel 2_rrrggbbb. */
  dlo_pixfmt_bgr565   = 1 | DLO_PIXFMT_2BYPP,                   /**< 16 bit per pixel 2_bbbbbggggggrrrrr. */
  dlo_pixfmt_rgb565   = 1 | DLO_PIXFMT_2BYPP | DLO_PIXFMT_SWP,  /**< 16 bit per pixel 2_rrrrrggggggbbbbb. */
  dlo_pixfmt_sbgr1555 = 2 | DLO_PIXFMT_2BYPP,                   /**< 16 bit per pixel 2_Sbbbbbgggggrrrrr (S is supremacy/transparancy bit). */
  dlo_pixfmt_srgb1555 = 2 | DLO_PIXFMT_2BYPP | DLO_PIXFMT_SWP,  /**< 16 bit per pixel 2_Srrrrrgggggbbbbb (S is supremacy/transparancy bit). */
  dlo_pixfmt_bgr888   = 3 | DLO_PIXFMT_3BYPP,                   /**< 24 bit per pixel 0xbbggrr. */
  dlo_pixfmt_rgb888   = 3 | DLO_PIXFMT_3BYPP | DLO_PIXFMT_SWP,  /**< 24 bit per pixel 0xrrggbb. */
  dlo_pixfmt_abgr8888 = 4 | DLO_PIXFMT_4BYPP,                   /**< 32 bit per pixel 0xaabbggrr. */
  dlo_pixfmt_argb8888 = 4 | DLO_PIXFMT_4BYPP | DLO_PIXFMT_SWP   /**< 32 bit per pixel 0xaarrggbb. */
  /* Any value greater than 1023 is assumed to be a pointer to: dlo_col32_t palette[256]
   * for translating paletted 8 bits per pixel data into colour numbers.
   */
} dlo_pixfmt_t;  /**< A struct @a dlo_pixfmt_s. */


/** A local (host-resident) bitmap or framebuffer. */
typedef struct dlo_fbuf_s
{
  uint16_t     width;        /**< Width (pixels). */
  uint16_t     height;       /**< Height (pixels). */
  dlo_pixfmt_t fmt;          /**< Pixel format (e.g. bits per pixel, colour component order, etc). */
  void        *base;         /**< Base address in host memory. */
  uint32_t     stride;       /**< Stride (pixels) from a pixel to the one directly below. */
} dlo_fbuf_t;                /**< A struct @a dlo_fbuf_s. */


/** An pointer to a location in the DisplayLink device logical memory map. */
typedef uint32_t dlo_ptr_t;


/** A viewport (bitmap) in a contiguous block of device memory.
 *
 *  A viewport describes the contents of a contiguous block of device memory (of which there
 *  is normally 16 MB available). It represents a rectangular bitmap which may be large
 *  enough to use as a screen mode or of any other non-zero size.
 *
 *  It is the responsibility of the caller to organise one or more viewports in the device
 *  memory and use the @c dlo_set_mode() call to determine which is visible on the screen
 *  at any given time.
 *
 *  When a device is first claimed, the EDID data for any connected display will be
 *  read. If successful, then an initial screen mode is set up using the native mode timings
 *  of the display. This will result in an initial viewport being defined at base address 0.
 *  Details of the initial viewport can be discovered by the caller using the
 *  @c dlo_get_mode() call. If no initial mode was set up by libdlo, then the caller must do
 *  so by making an appropriate call to @c dlo_set_mode().
 *
 *  Setting the screen mode does not result in the screen being cleared; the caller must make
 *  a call to @c dlo_fill_rect() to achieve this, e.g.
 *
 *    dlo_fill_rect(uid, NULL, NULL, DLO_RGB(0,0,0))
 *
 *  to clear the whole of the viewport for the current screen mode to black.
 *
 *  Overlapping viewports should be avoided because they do not include the concept of a
 *  'stride' (to get from one pixel to the pixel immediately below). They must also have a
 *  base address which starts on a two byte boundary. Aside from these constraints, viewports
 *  can be arranged at any locations within the device memory.
 *
 *  The caller should not assume anything about the format of the pixels stored in the device
 *  memory or how pixels are arranged within a given viewport's address range. It is safe to
 *  assume that the pixels will require three bytes each (in a 24 bpp viewport) so a viewport
 *  which is 1280 x 1024 pixels in size, starting at base address 0x000000 in the device
 *  memory will end at address 1280*1024*3 = 0x3C0000.
 *
 *  Thus, the caller may set up two screen banks by maintaining two viewports of the same
 *  dimensions, one starting at base address 0x000000 (for example) and another starting at
 *  base address 0x3C0000 (assuming they are 1280x1024 pixels in size). These two banks can
 *  be plotted to independently of each other and displayed using the @c dlo_set_mode() call.
 */
typedef struct dlo_view_s
{
  uint16_t  width;           /**< Width (pixels). */
  uint16_t  height;          /**< Height (pixels). */
  uint8_t   bpp;             /**< Colour depth (bits per pixel). */
  dlo_ptr_t base;            /**< Base address in the device memory. */
} dlo_view_t;                /**< A struct @a dlo_view_s. */


/** Descriptor for reading or setting the current screen mode. */
typedef struct dlo_mode_s
{
  dlo_view_t view;           /**< The @a dlo_view_t associated with the screen mode. */
  uint8_t    refresh;        /**< Refresh rate (Hz). */
} dlo_mode_t;                /**< A struct @a dlo_mode_s. */


/** Co-ordinates of a point/pixel relative to the origin, top-left, of a given @a dlo_view_t. */
typedef struct dlo_dot_s
{
  int32_t x;                 /**< X co-ordinate of the dot, pixels from left edge of screen (+ve is right). */
  int32_t y;                 /**< Y co-ordinate of the dot, pixels down from top edge of screen (-ve is up). */
} dlo_dot_t;                 /**< A struct @a dlo_dot_s. */


/** A rectangle relative to a given @a dlo_view_t. */
typedef struct dlo_rect_s
{
  dlo_dot_t origin;          /**< Origin co-ordinates (top-left of the rectangle). */
  uint16_t  width;           /**< Width (pixels) of rectangle (all pixels from origin.x to origin.x+width-1 inclusive). */
  uint16_t  height;          /**< Height (pixels) of rectangle (all pixels from origin.y to origin.x+height-1 inclusive). */
} dlo_rect_t;                /**< A struct @a dlo_rect_s. */


/** Return the meaning of the specified return code as a human-readable string.
 *
 *  @param  err  Return code.
 *
 *  @return  Pointer to zero-terminated (error) message string.
 *
 *  The string is held in libdlo's static workspace so no attempt should be made
 *  by the caller to free it.
 */
extern const char *dlo_strerror(const dlo_retcode_t err);


/** Initialisation call for libdlo.
 *
 *  @param  flags  Initialisation flags word (unused flags should be zero).
 *
 *  @return  Return code, zero for no error.
 *
 *  This function must be called by libdlo users before any other functions
 *  are called or they may result in undefined behaviour. It will ignore any
 *  flag bits which it does not currently understand.
 *
 *  The @a vsn value indicates the library API version number. As the API
 *  defined within this header file is evolved, the API version number will
 *  be incremented.
 */
extern dlo_retcode_t dlo_init(const dlo_init_t flags);


/** Finalisation call for libdlo.
 *
 *  @param  flags  Finalisation flags word (unused flags should be zero).
 *
 *  @return  Return code, zero for no error.
 *
 *  This function should be called when a program no longer needs to use libdlo
 *  (e.g. when it exits) in order to free up any system resources which have been
 *  claimed by this library. It will ignore any flag bits which it does not
 *  currently understand.
 */
extern dlo_retcode_t dlo_final(const dlo_final_t flags);


/** Map the caller's pointer to a udev structure from libusb to a unique ID in libdlo.
 *
 *  @param  udev  Pointer to USB device structure for given device (from libusb).
 *
 *  @return  Unique ID of the corresponding device for libdlo calls.
 *
 *  This call is intended for use by a caller that has already used libusb in order
 *  to identify a particular device that they want to connect to.
 */
extern dlo_dev_t dlo_lookup_device(uint32_t *udev);


/** Enumerate all connected DisplayLink-compatible devices.
 *
 *  @return  Pointer to linked list of device information structures (or NULL if none).
 *
 *  This function will return a pointer to one or more @a devlist_t structures if
 *  there are any active DisplayLink compatible devices connected to the computer.
 *  It will return a NULL pointer if none are found or if there was an error during
 *  the enumeration process.
 *
 *  It is the caller's responsibility to free the memory associated with this list
 *  by calling the @c dlo_free() function on each list item (but not for any
 *  strings pointed to by items in the list, as these strings are maintained by
 *  libdlo).
 */
extern dlo_devlist_t *dlo_enumerate_devices(void);


/** Claim the first available (unclaimed) device.
 *
 *  @param  flags    Flags word describing how the device is to be accessed.
 *  @param  timeout  Timeout in milliseconds (zero for infinite).
 *
 *  @return  Unique ID of the claimed device (or NULL if failed).
 *
 *  This call performs a very similar function to @c dlo_claim_device() except
 *  that it performs the enumeration of connected devices on behalf of the caller
 *  and returns the unique ID of the first available (unclaimed device). This
 *  device is claimed automatically.
 *
 *  Both because users will wish to use specific displays for specific purposes
 *  And because kernel framebuffer drivers and other device users aren't always
 *  counted as "claiming" the device, this interface is a bit dangerous.
 *  For "real" apps, use dlo_claim_default_device.  This API kept for the
 *  time being to provide an interface that "just works" if any DisplayLink
 *  device is present. 
 *
 *  If no unclaimed devices are found, or if the claim operation itself fails in
 *  some way, the function will return a device handle of zero.
 */
extern dlo_dev_t dlo_claim_first_device(const dlo_claim_t flags, const uint32_t timeout);

/** Claim the specified device.
 *
 *  @param  uid      Unique ID of the device to claim.
 *  @param  flags    Flags word describing how the device is to be accessed.
 *  @param  timeout  Timeout in milliseconds (zero for infinite).
 *
 *  @return  Unique ID of the claimed device (or NULL if failed).
 *
 *  Before accessing a device, it should be claimed in order to avoid clashes
 *  where multiple programs are trying to write to the same device (however,
 *  abstraction of that sort would be implemented above this API).
 *
 *  If the device is connected to a display which supports the EDID standard, then
 *  this call will also attempt to set up the display mode to the native resolution
 *  of the display - equivalent to a call to @c dlo_set_mode(). It will not clear
 *  the screen contents; you should call @c dlo_fill_rect() for that.
 *
 *  It is possible to specify a @a timeout value for this call. If a non-zero timeout
 *  is specified, the same value will be used for any further calls to the device.
 *  Passing in a zero timeout means that libdlo should use its default timeouts.
 *  The @a timeout value is only used for specific transactions with the device; in
 *  some special cases libdlo will still use its own internal timeout values.
 *
 *  Devices should be released with a call to @c release_device() when they are no
 *  longer required.
 */
extern dlo_dev_t dlo_claim_device(const dlo_dev_t uid, const dlo_claim_t flags, const uint32_t timeout);


/** Claim the default device.
 *
 *  @param  argc_p   Pointer to main program's argc.
 *  @param  argv     Main program's argv.
 *  @param  flags    Flags word describing how the device is to be accessed.
 *  @param  timeout  Timeout in milliseconds (zero for infinite).
 *
 *  @return  Unique ID of the claimed device (or NULL if failed).
 *
 *  This call enumerates the connected devices and returns the unique ID of the default
 *  device.  The default device is selected by the DLODISPLAY environment variable or the
 *  --dlo:display command-line option; if neither is specified the first device found is
 *  selected.  This selected device is claimed.
 *
 *  If no default device is found, or if the claim operation fails, the function will
 *  return a device handle of zero.
 *
 *  The main program should pass a pointer to argc and argv to this function so that any
 *  --dlo:display option can be found.  This function will update argc and argv to remove
 *  any --dlo:* options that it finds, so the main program need not worry about the
 *  presence of these options in the command line if it calls this function first.
 *  Pass NULL as argc_p to disable command-line parsing.
 */
extern dlo_dev_t dlo_claim_default_device(int *argc_p, char *argv[],
                                          const dlo_claim_t flags, const uint32_t timeout);


/** Release the specified device.
 *
 *  @param  uid  Unique ID of the device to release.
 *
 *  @return  Return code, zero for no error.
 *
 *  When a program is no longer interested in using a device which it earlier had
 *  claimed, it should release it with this call. This should still be called even
 *  in the case where communications with a device have failed (because the error
 *  may have been temporary). You can ignore errors from this call if you wish.
 */
extern dlo_retcode_t dlo_release_device(const dlo_dev_t uid);


/** Given the device unique ID, return the associated device information structure.
 *
 *  @param  uid  Unique ID of the device to access.
 *
 *  @return  Pointer to device information structure.
 *
 *  Note: the caller should not attempt to free the returned structure as it is
 *  maintained by libdlo.
 */
extern dlo_devinfo_t *dlo_device_info(const dlo_dev_t uid);


/** Set the screen mode by selecting the best matching mode available.
 *
 *  @param  uid   Unique ID of the device to access.
 *  @param  mode  Mode descriptor structure pointer.
 *
 *  @return  Return code, zero for no error.
 *
 *  This function will select a screen mode from the list of those supported by
 *  the specified device (if the display connected to a device supports EDID,
 *  then the supported mode list will be derived from the EDID information).
 *
 *  The @a mode parameter describes the desired mode parameters, some of which
 *  are optional and may be left as zero/NULL. The mode itself may be NULL.
 *
 *  @li width in pixels (or zero to use the best match against EDID)
 *  @li height in pixels (or zero to use the best match against EDID)
 *  @li colour depth in bits per pixel (currently, only 24 is supported)
 *  @li base address in the device memory (of the origin of the mode's viewport)
 *  @li refresh rate, in Hz (or zero to select the best match against EDID)
 *
 *  This call will not cause the screen area to be cleared to a 'background'
 *  colour.
 */
extern dlo_retcode_t dlo_set_mode(const dlo_dev_t uid, const dlo_mode_t * const mode);


/** Returns a block of information about the current screen mode.
 *
 *  @param  uid  Unique ID of the device to access (or zero).
 *
 *  @return  Pointer to a mode description structure.
 *
 *  This function will return a pointer to a structure describing the current
 *  screen mode for the specified device. Note: if no mode has been set since
 *  the device was claimed, this structure will be initialised to contain all
 *  zeros.
 *
 *  Note: the caller should not attempt to free the returned structure as it
 *  is maintained by libdlo.
 */
extern dlo_mode_t *dlo_get_mode(const dlo_dev_t uid);


/** Plot a filled rectangle into the specified device.
 *
 *  @param  uid   Unique ID of the device to access.
 *  @param  view  Struct pointer: the destination viewport.
 *  @param  rec   Struct pointer: co-ordinates of rectangle to plot (relative to viewport).
 *  @param  col   Colour of filled rectangle.
 *
 *  @return  Return code, zero for no error.
 *
 *  Plot a filled rectangle in the specified colour into a viewport on the device. The
 *  rectangle specified is relative to the viewport's origin and will be clipped if any
 *  parts of it lie outside of the viewport's extent. Setting both @a view and @a rec to
 *  NULL will effectively clear the current screen to the specified colour.
 *
 *  If @a view is NULL, then the current visible screen is used as the destination viewport.
 *  If @a rec is NULL, then the whole of the destination viewport is filled.
 */
extern dlo_retcode_t dlo_fill_rect(const dlo_dev_t uid,
                                   const dlo_view_t * const view, const dlo_rect_t * const rec,
                                   const dlo_col32_t col);


/** Copy a rectangular area within the device from one location to another.
 *
 *  @param  uid        Unique ID of the device to access.
 *  @param  src_view   Struct pointer: source viewport.
 *  @param  src_rec    Struct pointer: co-ordinates of rectangle to copy (relative to source viewport).
 *  @param  dest_view  Struct pointer: destination viewport.
 *  @param  dest_pos   Struct pointer: origin of copy destination (relative to destination viewport).
 *
 *  @return  Return code, zero for no error.
 *
 *  Copy a rectangular area of pixels from one location in the device to another. The source
 *  and destination rectangles may lie in the same viewport or different viewports - it is up
 *  to the caller to maintain their own list of how the device memory is organised in terms of
 *  viewports.
 *
 *  The source and destination viewports must not reference overlapping memory regions in the
 *  device, except in the case where both viewports describe exactly the same memory region.
 *  There are no constraints relating to whether or not source and destination rectangles
 *  overlap.
 *
 *  If @a src_view is NULL, then the current visible screen is used as the source viewport.
 *  If @a src_rec is NULL, then the whole of the source viewport is used as the source rectangle.
 *  If @a dest_view is NULL, then the current visible screen is used as the destination viewport.
 *  If @a dest_pos is NULL, then the origin (top-left) of the destination viewport is used.
 */
extern dlo_retcode_t dlo_copy_rect(const dlo_dev_t uid,
                                   const dlo_view_t * const src_view,  const dlo_rect_t * const src_rec,
                                   const dlo_view_t * const dest_view, const dlo_dot_t * const dest_pos);


/** Copy (and translate pixel formats) a rectangular area from host memory into the device.
 *
 *  @param  uid        Unique ID of the device to access.
 *  @param  flags      Flags word indicating special behaviour (unused flags should be zero).
 *  @param  fbuf       Struct pointer: information about source bitmap in host memory.
 *  @param  dest_view  Struct pointer: destination viewport.
 *  @param  dest_pos   Struct pointer: origin of copy destination (relative to destination viewport).
 *
 *  @return  Return code, zero for no error.
 *
 *  Transfer a bitmap held in the host memory to the device. The bitmap is treated as a rectangular
 *  area of pixels, possibly within a larger bitmap/framebuffer and will be plotted at the desired
 *  co-ordinates relative to the origin of the specified viewport. If any part of the bitmap falls
 *  outside of the viewport's extent, the region will be clipped.
 *
 *  If @a dest_view is NULL, then the current visible screen is used as the destination viewport.
 *  If @a dest_pos is NULL, then the origin (top-left) of the destination viewport is used.
 */
extern dlo_retcode_t dlo_copy_host_bmp(const dlo_dev_t uid, const dlo_bmpflags_t flags,
                                       const dlo_fbuf_t * const fbuf,
                                       const dlo_view_t * const dest_view, const dlo_dot_t * const dest_pos);

extern dlo_retcode_t dlo_check_device(uint8_t daddr);
extern dlo_retcode_t dlo_flush_usb(const dlo_dev_t uid, bool force);
__attribute__ ((weak))  void dlo_device_configured (dlo_dev_t uid);

#ifdef __cplusplus
};
#endif

#endif
