/** @file dlo_defs.h
 *
 *  @brief This file defines common macros and so on for the DisplayLink libdlo library.
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

#ifndef DLO_DLOCOMMON_H
#define DLO_DLOCOMMON_H        /**< Avoid multiple inclusion. */

#include <stdint.h>
#include <stdio.h>

#ifdef DEBUG

/** Assert that a given expression evaluates to a non-zero number.
 *
 *  @param  expr  The expression to test.
 */
#define ASSERT(expr) if (!(expr)) { printf("<%s:%u>\n  Assertion '%s' failed.\n", __FILE__, __LINE__, #expr); exit(1); }

/** Variadic debugging printf() implementation.
 *
 *  @param  fmt  Format string for printed output.
 */
#define DPRINTF(fmt, ...) printf(fmt, ##__VA_ARGS__)

#else

/** Non-debug assert - does nothing.
 *
 *  @param  expr  The expression to test.
 */
#define ASSERT(expr)

/** Non-debug printf - does nothing.
 *
 *  @param  fmt  Format string for printed output.
 */
#define DPRINTF(fmt, ...)

#endif

/** Get a couple of required attributes into a handy format. */
#if __GNUC__ >= 3

/* Check for portable attribute definitions and create them if they are missing */
#ifndef inline
#define inline inline __attribute__ ((always_inline))
#endif
#ifndef __packed
#define __packed __attribute__ ((packed))
#endif

#else

/* Toolchain is too old or doesn't support the attributes we need */
#ifndef inline
#define inline TOOLCHAIN_ERROR_NO_INLINE
#endif
#ifndef __packed
#define __packed TOOLCHAIN_ERROR_NO_PACKED
#endif

#endif

/** Read an unsigned 32 bit value from the address given (may be unaligned). */
#define RD_L(ptr) (*(__packed uint32_t *)(ptr))

/** Read an unsigned 16 bit value from the address given (may be unaligned). */
#define RD_S(ptr) (*(__packed uint16_t *)(ptr))

/** Read an unsigned byte from the address given. */
#define RD_B(ptr) (*(uint8_t *)(ptr))

/** Surpress warnings about unused variables. */
#define IGNORE(x) do { (void) (x); } while (0)

/** Record the line and source file associated with a particular type of error event. */
#define REC_ERR() { snprintf(&err_file[0], 1024, "%s", __FILE__); err_line = __LINE__; }

/** If a called function (cmd) returns an error code, return the error code from the calling function. */
#define ERR(cmd) do { dlo_retcode_t __err = (cmd); if (__err != dlo_ok) return __err; } while(0)

/** If a usb_ function call returns an error code, return an error and store the code in the @a usberr global. */
#define UERR(cmd) do { usberr = (cmd); if (usberr < 0) return usb_error_grab(); } while (0)

/** If a memory-allocating call returns NULL, return with a memory allocation error code. */
#define NERR(ptr) do { if (!(ptr)) { REC_ERR(); return dlo_err_memory; } } while (0)

/** If a function call (cmd) returns an error code, jump to the "error" label. Requires variable declaration: dlo_retcode_t err; */
#define ERR_GOTO(cmd) do { err = (cmd); if (err != dlo_ok) goto error; } while(0)

/** If a usb_ function call returns an error code, jump to the "error" label and store the usb error code in the @a usberr global. */
#define UERR_GOTO(cmd) do { usberr = (cmd); if (usberr < 0) { err = usb_error_grab(); goto error; } } while(0)

/** If a memory-allocating call returns NULL, jump to the "error" label with a memory allocation error code. */
#define NERR_GOTO(ptr) do { if (!(ptr)) { REC_ERR(); err = dlo_err_memory; goto error; } } while(0)

/** Return the size of a hash-defined string, excluding the terminator (unlike strlen(), it does count null bytes in the string). */
#define DSIZEOF(str) (sizeof(str) - 1)

/** Swap two integer values (assumes they are the same size). */
#define SWAP(a, b) do { (a) = (a) ^ (b); (b) = (a) ^ (b); (a) = (a) ^ (b); } while (0)

/** Magic macro for calling functions from pointer tables (after first checking the device structure pointer and the function pointer). */
#define CALL(dev, fn, ...) dev ? ( dev->fn ? dev->fn(dev, ##__VA_ARGS__) : dlo_err_unsupported ) : dlo_err_bad_device

/** Number of bytes per 16 bpp pixel! */
#define BYTES_PER_16BPP (2)

/** Number of bytes per 8 bpp pixel! */
#define BYTES_PER_8BPP (1)

/** Convert a framebuffer pixel format into a number of bytes per pixel. */
#define FORMAT_TO_BYTES_PER_PIXEL(fmt) ((unsigned int)fmt > 1023u ? 1 : ((fmt) & DLO_PIXFMT_BYPP_MSK) >> DLO_PIXFMT_BYPP_SFT)

/** Default buffer size for sending commands to the device. */
#define BUF_SIZE (64*1024u)

/** Threshold (bytes away from being full) for flushing the command buffer before adding any more commands to it. */
#define BUF_HIGH_WATER_MARK (1*1024u)

/** A 16 bits per pixel colour number (not normally used outside libdlo). */
typedef uint16_t dlo_col16_t;

/** An 8 bits per pixel colour number (2_rrrggbbb - not normally used outside libdlo). */
typedef uint8_t dlo_col8_t;

/** Integer used to store error number for last failed call to a dlo_usb_ function. */
extern int32_t usberr;

/** Integer used to store error number for last failed call to a dlo_eth_ function. */
extern int32_t etherr;

/** Source file name for the last recorded error event. */
extern char err_file[1024];

/** Source line number for last recorded error event. */
extern uint32_t err_line;


#endif
