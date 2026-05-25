/*
 *
 *  @brief Implementation of the screen mode-related functions.
 *
 *  See dlo_mode.h for more information.
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

#include <stdio.h>
#include <string.h>
#include "dlo_defs.h"
#include "dlo_mode.h"
#include "dlo_data.h"
#include "dlo_usb.h"


/* File-scope defines ------------------------------------------------------------------*/


/** Pre-defined EDID header used to check that data read from a device is valid.
 */
const uint8_t header[8] =
{
  0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

/** Constant used for deciding if a host is big- or little endian.
 */
const uint32_t endian = 1;

/** Unlock video registers command.
 */
#define WRITE_VIDREG_LOCK "\xAF\x20\xFF\x00"

/** Lock video registers command.
 */
#define WRITE_VIDREG_UNLOCK "\xAF\x20\xFF\xFF\xAF\xA0"

/** Command bytes needed to update both 16 bpp and 8 bpp frame base registers. */
#define SET_BASE_COMMAND_BYTES (DSIZEOF(WRITE_VIDREG_LOCK) + (6u * 4u) + DSIZEOF(WRITE_VIDREG_UNLOCK))

/** Return non-zero if the host is big endian or zero if the host is little endian.
 */
#define IS_BIGENDIAN() ((*(char*)&endian) == '\0')

/** Convert a 32 bit little endian value into a host byte-order value.
 *
 *  @param  val  Little-endian 32 bit value to convert.
 *
 *  @return  Value in host byte order.
 */
#define LETOHL(val) (IS_BIGENDIAN() ? swap_endian_l(val) : val)

/** Convert a 16 bit little endian value into a host byte-order value.
 *
 *  @param  val  Little-endian 16 bit value to convert.
 *
 *  @return  Value in host byte order.
 */
#define LETOHS(val) (IS_BIGENDIAN() ? swap_endian_s(val) : val)

/** Initialise a @a dlo_mode_data array entry.
 *
 *  @param  idx       Array index.
 *  @param  mwidth    Mode width (pixels).
 *  @param  mheight   Mode height (pixels).
 *  @param  mrefresh  Mode refresh rate (Hz).
 *  @param  mbpp      Mode colour depth (bits per pixel).
 */
#define init_mode(idx, mwidth, mheight, mrefresh, mbpp)                                                    \
{                                                                                                          \
  dlo_mode_data[idx].width      = mwidth;                                                                  \
  dlo_mode_data[idx].height     = mheight;                                                                 \
  dlo_mode_data[idx].refresh    = mrefresh;                                                                \
  dlo_mode_data[idx].bpp        = mbpp;                                                                    \
  dlo_mode_data[idx].data       = DLO_MODE_DATA_##mwidth##_##mheight##_##mrefresh##_##mbpp##_0;            \
  dlo_mode_data[idx].data_sz    = DSIZEOF(DLO_MODE_DATA_##mwidth##_##mheight##_##mrefresh##_##mbpp##_0);   \
  dlo_mode_data[idx].mode_en    = DLO_MODE_ENABLE_##mwidth##_##mheight##_##mrefresh##_##mbpp##_0;          \
  dlo_mode_data[idx].mode_en_sz = DSIZEOF(DLO_MODE_ENABLE_##mwidth##_##mheight##_##mrefresh##_##mbpp##_0); \
  dlo_mode_data[idx].low_blank  = false;                                                                   \
}


/* External-scope inline functions -----------------------------------------------------*/


/** Swap the endianness of a long (four byte) integer.
 *
 *  @param  val  Integer to alter.
 *
 *  @return  Value with swapped endianness.
 */
inline uint32_t swap_endian_l(uint32_t val)
{
  return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val & 0xFF0000) >> 8) | (( val >> 24) & 0xFF);
}


/** Swap the endianness of a short (two byte) integer.
 *
 *  @param  val  Integer to alter.
 *
 *  @return  Value with swapped endianness.
 */
inline uint16_t swap_endian_s(uint16_t val)
{
  return ((val >> 8) & 0xFF) | ((val << 8) & 0xFF00);
}


extern uint32_t swap_endian_l(uint32_t val);
extern uint16_t swap_endian_s(uint16_t val);


/* File-scope types --------------------------------------------------------------------*/


/** Structure to hold information about a specific screen mode.
 */
typedef struct dlo_mode_data_s
{
  uint16_t width;        /**< Width (pixels). */
  uint16_t height;       /**< Height (pixels). */
  uint8_t  refresh;      /**< Refresh rate (Hz). */
  uint8_t  bpp;          /**< Colour depth (bits per pixel). */
  char    *data;         /**< Block of mode data. */
  size_t   data_sz;      /**< Size of mode data block (bytes). */
  char    *mode_en;      /**< Block of mode enable data. */
  size_t   mode_en_sz;   /**< Size of mode enable data block (bytes). */
  bool     low_blank;    /**< Screen mode has reduced blanking. */
} dlo_mode_data_t;       /**< A struct @a dlo_mode_data_s. */


/* External scope variables ------------------------------------------------------------*/


/* File-scope variables ----------------------------------------------------------------*/


/** Array of hard-wired screen mode definitions.
 */
static dlo_mode_data_t dlo_mode_data[DLO_MODE_DATA_NUM];


/** Mode information corresponding with flag bits in EDID establisted timings bytes.
 */
const est_timing_t est_timings[24] =
{
  { 800, 600, 60 },        /* bit 0 */
  { 800, 600, 56 },        /* bit 1 */
  { 640, 480, 75 },        /* bit 2 */
  { 640, 480, 72 },        /* bit 3 */
  { 640, 480, 67 },        /* bit 4 */
  { 640, 480, 60 },        /* bit 5 */
  { 720, 400, 88 },        /* bit 6 */
  { 720, 400, 70 },        /* bit 7 */
  { 1280, 1024, 75 },      /* bit 8 */
  { 1024, 768, 75 },       /* bit 9 */
  { 1024, 768, 70 },       /* bit 10 */
  { 1024, 768, 60 },       /* bit 11 */
  { 1024, 768, 87 },       /* bit 12 */
  { 832, 624, 75 },        /* bit 13 */
  { 800, 600, 75 },        /* bit 14 */
  { 800, 600, 72 },        /* bit 15 */
  { 0, 0, 0 },             /* bit 16 */
  { 0, 0, 0 },             /* bit 17 */
  { 0, 0, 0 },             /* bit 18 */
  { 0, 0, 0 },             /* bit 19 */
  { 0, 0, 0 },             /* bit 20 */
  { 0, 0, 0 },             /* bit 21 */
  { 0, 0, 0 },             /* bit 22 */
  { 1152, 870, 75 }        /* bit 23 */
};


/* File-scope function declarations ----------------------------------------------------*/


/** Calcualte refresh rate in hertz 
 *  
 *  @param did  A parsed, unpacked EDID detailed timing structure
 *  
 *  @return refresh rate in hertz
 */
inline uint8_t refresh_hz_from_edid(edid_detail_unpacked_t const * edid) {
  return (uint8_t)(edid->pixelClock10KHz*10000/
    ((edid->hBlanking + edid->hActive)*(edid->vBlanking + edid->vActive)))+0.5;
}

/** Append a video register setting command onto the specified char block.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *  @param  reg  Register number (0..255).
 *  @param  val  Value to set (byte).
 *
 *  @return  Return code, zero for no error.
 */
static dlo_retcode_t vreg(dlo_device_t * const dev, uint8_t reg, uint8_t val);


/** Append a video register setting command onto the specified char block.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *  @param  reg  Register number (0..255).
 *  @param  val  Value to set (byte).
 *
 *  @return  Return code, zero for no error.
 */
static dlo_retcode_t vbuf(dlo_device_t * const dev, const char * const buf, size_t len);


/** Check an EDID checksum to see if it is invalid.
 *
 *  @param  ptr   Pointer to base of EDID structure data.
 *  @param  size  Size of data (bytes).
 *
 *  @return  true if checksum is incorrect, false if OK.
 */
static bool bad_edid_checksum(const uint8_t * const ptr, const size_t size);


/** Parse an EDID detailed timing descriptor/mode descriptor.
 *
 *  @param  monitor  Pointer to descriptor structure to initialise.
 *  @param  ptr      Pointer to base of descriptor in EDID data to parse.
 */
static dlo_retcode_t unpack_edid_detailed_timing(edid_detail_unpacked_t * const desc, const uint8_t * const ptr);

/** Set mode based on preferred/default form EDID detailed timing descriptor/mode descriptor.
 *
 *  @param  dev      Pointer to @a dlo_device_t structure. 
 *  @param  timing   Pointer to @a edid_detail_unpacked_t structure.
 */
static dlo_retcode_t mode_set_from_edid(dlo_device_t * const dev, edid_detail_unpacked_t *edid, uint32_t base);


/** Parse EDID colour characteristics.
 *
 *  @param  cols  Pointer to colour structure to initialise.
 *  @param  ptr   Pointer to base of colour characteristics block to parse from EDID data.
 */
static void parse_edid_colours(edid_colours_t * const cols, const uint8_t * const ptr);


/** Look for a mode definition amongst the list of known modes.
 *
 *  @param  dev      Pointer to @a dlo_device_t structure.
 *  @param  width    Width of desired display (pixels).
 *  @param  height   Hieght of desired display (pixels) - zero to select first available.
 *  @param  refresh  Desired refresh rate (Hz) - zero to select first available.
 *  @param  bpp      Colour depth (bits per pixel) - zero to select first available.
 *
 *  @return  Mode number of the best matching mode definition (or @a DLO_INVALID_MODE if none found).
 */
static dlo_modenum_t get_mode_number(dlo_device_t * const dev, const uint16_t width, const uint16_t height, const uint8_t refresh, const uint8_t bpp);


/** Given a bitmap and a mode number, set the current screen mode.
 *
 *  @param  bmp   Bitmap to set as the current rastered display mode.
 *  @param  mode  Mode number for best match to the supplied bitmap.
 *
 *  @return  Return code, zero for no error.
 */
static dlo_retcode_t mode_select(dlo_device_t * const dev, const dlo_mode_t * const desc, const dlo_modenum_t mode);


/** Look up the specified mode and add to the supported list if found.
 *
 *  @param  dev      Pointer to @a dlo_device_t structure.
 *  @param  idx      Index - incrememnted if the mode was found.
 *  @param  width    Width of the mode to look for (pixels).
 *  @param  height   Height of the mode to look for (pixels).
 *  @param  refresh  Refresh rate of the mode to look for (Hz).
 *
 *  @return  Updated index (as @a idx if mode not found, else incremented by one).
 */
static uint16_t add_supported(dlo_device_t * const dev, uint16_t idx, const uint16_t width, const uint16_t height, const uint8_t refresh);


/** Build a list of supported modes, based upon the supplied EDID information.
 *
 *  @param  dev   Device to update.
 *  @param  edid  Pointer to structure holding parsed EDID information.
 *
 *  @return  Return code, zero for no error.
 */
static dlo_retcode_t lookup_edid_modes(dlo_device_t * const dev, const dlo_edid_t * const edid);


/** Program the base addresses of the video display in the device.
 *
 *  @param  dev    Pointer to @a dlo_device_t structure.
 *  @param  base   Address of the base of the 16 bpp segment.
 *  @param  base8  Address of the base of the 8 bpp segment.
 *
 *  @return  Return code, zero for no error.
 *
 *  Note: this call first will cause any buffered commands to be sent to the device then
 *  the set base commands are sent. The buffer to that device is thus flushed.
 */
static dlo_retcode_t set_base(dlo_device_t * const dev, const dlo_ptr_t base, const dlo_ptr_t base8);


/* Public function definitions ---------------------------------------------------------*/


dlo_retcode_t dlo_mode_init(const dlo_init_t flags)
{
  init_mode(0,  1920, 1080, 60, 24);
  init_mode(1,  1600, 1200, 60, 24);
  init_mode(2,  1400, 1050, 85, 24);
  init_mode(3,  1400, 1050, 75, 24);
  init_mode(4,  1400, 1050, 60, 24);
  init_mode(5,  1400, 1050, 60, 24);
  init_mode(6,  1366, 768,  60, 24);
  init_mode(7,  1360, 768,  60, 24);
  init_mode(8,  1280, 960,  85, 24);
  init_mode(9,  1280, 960,  60, 24);
  init_mode(10, 1280, 800,  60, 24);
  init_mode(11, 1280, 768,  85, 24);
  init_mode(12, 1280, 768,  75, 24);
  init_mode(13, 1280, 1024, 85, 24);
  init_mode(14, 1280, 1024, 75, 24);
  init_mode(15, 1280, 1024, 60, 24);
  init_mode(16, 1280, 768,  60, 24);
  init_mode(17, 1152, 864,  75, 24);
  init_mode(18, 1024, 768,  85, 24);
  init_mode(19, 1024, 768,  75, 24);
  init_mode(20, 1024, 768,  70, 24);
  init_mode(21, 1024, 768,  60, 24);
  init_mode(22, 848,  480,  60, 24);
  init_mode(23, 800,  600,  85, 24);
  init_mode(24, 800,  600,  75, 24);
  init_mode(25, 800,  600,  72, 24);
  init_mode(26, 800,  600,  60, 24);
  init_mode(27, 800,  600,  56, 24);
  init_mode(28, 800,  480,  60, 24);
  init_mode(29, 720,  400,  85, 24);
  init_mode(30, 720,  400,  70, 24);
  init_mode(31, 640,  480,  85, 24);
  init_mode(32, 640,  480,  75, 24);
  init_mode(33, 640,  480,  73, 24);
  init_mode(34, 640,  480,  60, 24);

  return dlo_ok;
}


dlo_retcode_t dlo_mode_final(const dlo_final_t flags)
{
  return dlo_ok;
}


dlo_mode_t *dlo_mode_from_number(const dlo_modenum_t num)
{
  static dlo_mode_t mode;

  if (num < DLO_MODE_DATA_NUM)
  {
    mode.view.width  = dlo_mode_data[num].width;
    mode.view.height = dlo_mode_data[num].height;
    mode.view.bpp    = dlo_mode_data[num].bpp;
    mode.view.base   = 0;
    mode.refresh     = dlo_mode_data[num].refresh;
    return &mode;
  }
  return NULL;
}


dlo_modenum_t dlo_mode_lookup(dlo_device_t * const dev, const uint16_t width, const uint16_t height, const uint8_t refresh, uint8_t bpp)
{
  /* Check that the requested screen mode is supported */
  //DPRINTF("mode: lookup: %ux%u @ %u Hz, %u bpp\n", width, height, refresh, bpp);
  return bpp != 24 ? DLO_INVALID_MODE : get_mode_number(dev, width, height, refresh, bpp);
}


dlo_retcode_t dlo_mode_change(dlo_device_t * const dev, const dlo_mode_t * const desc, dlo_modenum_t mode)
{
  /* If no mode number was specified on entry, try looking one up for the supplied bitmap */
  if (mode == DLO_INVALID_MODE)
    mode = get_mode_number(dev, desc->view.width, desc->view.height, 0, desc->view.bpp);

  /* Change mode or return an error */
  return mode_select(dev, desc, mode);
}

dlo_retcode_t dlo_mode_set_default(dlo_device_t * const dev, uint32_t base)
{
  /* The first timing block is the preferred mode of the monitor */
  DPRINTF("mode: dlo_mode_set_default (setting monitor preferred mode)\n");
  return mode_set_from_edid(dev, &dev->edid.timings[0], base);
}

dlo_retcode_t dlo_mode_set_base(dlo_device_t * const dev, dlo_ptr_t base)
{
  dlo_ptr_t base8;
  dlo_retcode_t err;

  if (!dev)
    return dlo_err_bad_device;

  if (!dev->mode.view.width || !dev->mode.view.height)
    return dlo_err_bad_mode;

  if (base & 1)
    return dlo_err_bad_mode;

  /* Keep the base switch ordered after pending drawing commands, flushing only
   * if there is not enough room to append it to the same bulk transfer.
   */
  if ((size_t)(dev->bufend - dev->bufptr) < SET_BASE_COMMAND_BYTES)
  {
    err = dlo_usb_write(dev);
    if (dlo_ok != err)
      return err;
  }

  base8 = base + (BYTES_PER_16BPP * dev->mode.view.width * dev->mode.view.height);
  err = set_base(dev, base, base8);
  if (dlo_ok != err)
    return err;

  dev->mode.view.base = base;
  dev->base8 = base8;
  return dlo_ok;
}


dlo_retcode_t dlo_mode_parse_edid(dlo_device_t * const dev, const uint8_t * const ptr, const size_t size)
{
  dlo_edid_t *edid = &dev->edid;
  uint32_t             i;

  /* Copy the header bytes straight into our structure (assumes the structure is correct size!) */
  ASSERT(EDID_STRUCT_SZ == size);
  if (memcmp(header, ptr, sizeof(header)))
    return dlo_err_edid_fail;

  if (bad_edid_checksum(ptr, size))
    return dlo_err_edid_fail;

#if (defined(DEBUG) && 0) /* If you really want this verbose debug output, change the 0 to a 1 */
  uint32_t j;
  /* Parse the vendor/product information */
  for (j = 0; j < 128; j += 8)
  {
    DPRINTF("mode: edid: raw: ");
    for (i = 0; i < 8; i++)
      DPRINTF("%02X=%02X ", i + j, RD_B(ptr + i + j));
    DPRINTF("\n");
  }
#endif

  edid->product.manuf_name = LETOHS(RD_S(ptr + 0x08));
  edid->product.prod_code  = LETOHS(RD_S(ptr + 0x0A));
  edid->product.serial_num = LETOHL(RD_L(ptr + 0x0C));
  edid->product.manuf_wk   = RD_B(ptr + 0x10);
  edid->product.manuf_yr   = RD_B(ptr + 0x11);
  //DPRINTF("mode: edid: manuf &%X prod &%X serial &%X wk %u yr %u\n",
  //        edid->product.manuf_name, edid->product.prod_code, edid->product.serial_num,
  //        edid->product.manuf_wk, edid->product.manuf_yr + 1990);

  /* Parse the EDID structure information */
  edid->version.number   = RD_B(ptr + 0x12);
  edid->version.revision = RD_B(ptr + 0x13);
  //DPRINTF("edid: version &%02X revision &%02X\n", edid->version.number, edid->version.revision);

  /* Parse the basic dislpay parameters/features */
  edid->basic.input_def    = RD_B(ptr + 0x14);
  edid->basic.max_horiz_sz = RD_B(ptr + 0x15);
  edid->basic.max_vert_sz  = RD_B(ptr + 0x16);
  edid->basic.gamma        = (100.0 + RD_B(ptr + 0x17)) / 100.0;
  edid->basic.features     = RD_B(ptr + 0x18);

  /* Parse the colour characteristics */
  parse_edid_colours(&(edid->colours), ptr + 0x19);

  /* Parse the established timings */
  edid->est_timings.timings[0] = RD_B(ptr + 0x23);
  edid->est_timings.timings[1] = RD_B(ptr + 0x24);
  edid->est_timings.resvd      = RD_B(ptr + 0x25);

  /* Parse the bits at the end of the EDID structure */
  edid->ext_blocks = RD_B(ptr + 0x7E);

  /* Parse all of the standard timing identification */
  for (i = 0; i < sizeof(edid->std_timings.timing_id); i++)
    edid->std_timings.timing_id[i] = RD_B(ptr + 0x26 + i);

  /* Parse all of the detailed timing descriptions (monitor descriptors not supported) */
  for (i = 0; i < 4; i++) {
    unpack_edid_detailed_timing(&edid->timings[i], ptr + 0x36 + (i * 0x12));
  }
  return lookup_edid_modes(dev, edid);
}


void use_default_modes(dlo_device_t * const dev)
{
  uint32_t i;

  for (i = 0; i < DLO_MODE_DATA_NUM; i++)
    dev->supported[i] = (dlo_modenum_t)i;
}


/* File-scope function definitions -----------------------------------------------------*/


static dlo_retcode_t vreg(dlo_device_t * const dev, uint8_t reg, uint8_t val)
{
  if (dev->bufend - dev->bufptr < 4)
    return dlo_err_buf_full;

  *(dev->bufptr)++ = '\xAF';
  *(dev->bufptr)++ = '\x20';
  *(dev->bufptr)++ = reg;
  *(dev->bufptr)++ = val;

  return dlo_ok;
}


static dlo_retcode_t vbuf(dlo_device_t * const dev, const char * buf, size_t len)
{
  if (dev->bufend - dev->bufptr < len)
    return dlo_err_buf_full;

  while (len--)
    *(dev->bufptr)++ = *buf++;

  return dlo_ok;
}


static dlo_modenum_t get_mode_number(dlo_device_t * const dev, const uint16_t width, const uint16_t height, const uint8_t refresh, const uint8_t bpp)
{
  dlo_modenum_t idx;
  uint32_t       num;

  /* Look for all matching modes in our array of video timing structures.
   *
   * Note: if we don't have EDID data for the monitor attached to the device
   * we simply look through all the modes we have, rather than only looking
   * at the supported modes list.
   */
  for (idx = 0; idx < DLO_MODE_DATA_NUM; idx++)
  {
    /* Read the mode number from the device's supported modes array */
    num = dev->supported[idx];
    if (num == DLO_INVALID_MODE) {
      break;
    }
    /* This sequence of if statements looks odd, take care if you decide to 'optimise' it! */
    if (dlo_mode_data[num].width != width)
      continue;
    if (bpp && dlo_mode_data[num].bpp != bpp)
      continue;
    if (height && dlo_mode_data[num].height != height)
      continue;
    if (refresh && dlo_mode_data[num].refresh != refresh)
      continue;

    /* If we're satisfied with the tests above, then return the mode number */
    return num;
  }

  /* No matches found, return an invalid mode number */
  return DLO_INVALID_MODE;
}

/** Perform lfsr16 encoding of the argument.
 *
 *  @param  v  Value to transform.
 *
 *  @return  Encoded value.
 *
 *  Inefficient computation of a value as encoded by an 16 bit linear feedback
 *  shift register. Better implementations might use look-up tables or the like
 *  (which would require around 128KB of RAM).
 * 
 *  LFSR isn't actually in the mode registers for encryption purposes
 * (which is why it doesn't apply to all registers).  It's actually
 * for convenience on the hardware side, as the registers are used as counters
 */
static uint16_t lfsr16(uint16_t v)
{
  uint32_t _v   = 0xFFFF;

  while (v--) {
    _v = ((_v << 1) | (((_v >> 15) ^ (_v >> 4) ^ (_v >> 2) ^ (_v >> 1)) & 1)) & 0xFFFF;
  }
  return (uint16_t) _v;
}

inline dlo_retcode_t vreg_big_endian(dlo_device_t * const dev, uint8_t reg, uint16_t val){

  if (IS_BIGENDIAN()) {
    val = swap_endian_s(val);
  }

  ERR(vreg(dev, reg, val >> 8));
  ERR(vreg(dev, reg+1, val & 0xFF));

  return dlo_ok;
}

inline dlo_retcode_t vreg_little_endian(dlo_device_t * const dev, uint8_t reg, uint16_t val){


  if (IS_BIGENDIAN()) {
    val = swap_endian_s(val);
  }

  ERR(vreg(dev, reg, val & 0xFF));
  ERR(vreg(dev, reg+1, val >> 8));

  return dlo_ok;
}

inline dlo_retcode_t vreg_lfsr16(dlo_device_t * const dev, uint8_t reg, uint16_t val){

  uint16_t lsfr_shifted = lfsr16(val);

  return vreg_big_endian(dev, reg, lsfr_shifted);
}

static dlo_retcode_t edid_to_vreg_commands(dlo_device_t * const dev, edid_detail_unpacked_t *edid, uint8_t color_depth)
{

  uint16_t xDisplayStart = edid->hBlanking - edid->hSyncOffset;
  uint16_t xDisplayEnd = xDisplayStart + edid->hActive;
  uint16_t yDisplayStart = edid->vSyncOffset > edid->vBlanking ? 0 : edid->vBlanking - edid->vSyncOffset;
  uint16_t yDisplayEnd = yDisplayStart + edid->vActive;
  uint16_t xEndCount = edid->hActive + edid->hBlanking - 1;
  uint16_t hSyncStart = 1;
  uint16_t hSyncEnd = edid->hSyncPulseWidth + 1;
  uint16_t hPixels = edid->hActive;
  uint16_t yEndCount = edid->vActive + edid->vBlanking;
  uint16_t vSyncStart = 0;
  uint16_t vSyncEnd = edid->vSyncPulseWidth;
  uint16_t vPixels = edid->vActive;
  uint16_t pixelClock5KHz = edid->pixelClock10KHz*2;

  if (!(edid->bHSyncPositive)) { SWAP(hSyncStart, hSyncEnd); } 
  if (!(edid->bVSyncPositive)) { SWAP(vSyncStart, vSyncEnd); }

  DPRINTF("dl_vreg: xDisplayStart:   %u\n", xDisplayStart);
  DPRINTF("dl_vreg: xDisplayEnd:     %u\n", xDisplayEnd);
  DPRINTF("dl_vreg: yDisplayStart:   %u\n", yDisplayStart);
  DPRINTF("dl_vreg: yDisplayEnd:     %u\n", yDisplayEnd);
  DPRINTF("dl_vreg: xEndCount:       %u\n", xEndCount);
  DPRINTF("dl_vreg: hSyncStart:      %u\n", hSyncStart);
  DPRINTF("dl_vreg: hSyncEnd:        %u\n", hSyncEnd);
  DPRINTF("dl_vreg: hPixels:         %u\n", hPixels);
  DPRINTF("dl_vreg: yEndCount:       %u\n", yEndCount);
  DPRINTF("dl_vreg: vSyncStart:      %u\n", vSyncStart);
  DPRINTF("dl_vreg: vSyncEnd:        %u\n", vSyncEnd);
  DPRINTF("dl_vreg: vPixels:         %u\n", vPixels); 
  DPRINTF("dl_vreg: Pixel Clock:     %u KHz\n", pixelClock5KHz * 5);

  /* Begin writing to video registers */
  //ERR(vreg(dev,               0xFF, 0));
 
  ERR(vreg(dev,               0x00, (color_depth == 16) ? 0 : 1));
  ERR(vreg_lfsr16(dev,        0x01, xDisplayStart));
  ERR(vreg_lfsr16(dev,        0x03, xDisplayEnd));
  ERR(vreg_lfsr16(dev,        0x05, yDisplayStart));
  ERR(vreg_lfsr16(dev,        0x07, yDisplayEnd)); 
  ERR(vreg_lfsr16(dev,        0x09, xEndCount)); 
  ERR(vreg_lfsr16(dev,        0x0B, hSyncStart)); 
  ERR(vreg_lfsr16(dev,        0x0D, hSyncEnd));
  ERR(vreg_big_endian(dev,    0x0F, hPixels));
  ERR(vreg_lfsr16(dev,        0x11, yEndCount));
  ERR(vreg_lfsr16(dev,        0x13, vSyncStart));
  ERR(vreg_lfsr16(dev,        0x15, vSyncEnd));
  ERR(vreg_big_endian(dev,    0x17, vPixels));
  ERR(vreg_little_endian(dev, 0x1B, pixelClock5KHz));

  /* wake monitor: enable hsync, vsync, and video */
  ERR(vreg(dev,               0x1F, 0));  

  /* flush register changes (write to reg 0xFF) and final null comand */
  ERR(vbuf(dev, WRITE_VIDREG_UNLOCK, DSIZEOF(WRITE_VIDREG_UNLOCK)));

  return dlo_ok;
}

static dlo_retcode_t mode_set_from_edid(dlo_device_t * const dev, edid_detail_unpacked_t *edid, uint32_t base) {

  /* EDID standard for detecting if detailed block is populated */
  if (edid->pixelClock10KHz == 0)
    return DLO_INVALID_MODE;

  /* Flush the command buffer */
  if (dlo_ok != dlo_usb_write(dev))
    return DLO_INVALID_MODE;

  /* Base address must be aligned to a two byte boundary */
  if (base & 1)
    return dlo_err_bad_mode;

  /* Select the standard output channel */
  ERR(dlo_usb_std_chan(dev));

  dev->mode.view.base = base;
  dev->base8          = base + (BYTES_PER_16BPP * edid->hActive * edid->vActive);
  ERR(set_base(dev, dev->mode.view.base, dev->base8));

  ERR(edid_to_vreg_commands(dev, edid, 24));

  /* Flush the command buffer */
  ERR(dlo_usb_write(dev));

  /* Revert channel back ? */
  ERR(dlo_usb_chan_sel(dev, DLO_MODE_POSTAMBLE, DSIZEOF(DLO_MODE_POSTAMBLE)));

  /* Update the device with the new mode details */
  dev->mode.view.width = edid->hActive;
  dev->mode.view.height = edid->vActive;
  dev->mode.view.bpp = 24;
  dev->mode.refresh = refresh_hz_from_edid(edid);

  DPRINTF("mode: mode_set_from_edid %ux%u @ %u Hz %u bpp (base &%X base8 &%X)\n",
          dev->mode.view.width, dev->mode.view.height, dev->mode.refresh, dev->mode.view.bpp,
          dev->mode.view.base, dev->base8);

  return dlo_ok;

}

static dlo_retcode_t mode_select(dlo_device_t * const dev, const dlo_mode_t * const desc, const dlo_modenum_t mode)
{
  if (mode >= DLO_MODE_DATA_NUM || mode == DLO_INVALID_MODE)
    return dlo_err_bad_mode;

  /* Base address must be aligned to a two byte boundary */
  if (desc->view.base & 1)
    return dlo_err_bad_mode;

  /* Flush the command buffer */
  if (dlo_ok != dlo_usb_write(dev))
    return DLO_INVALID_MODE;

  dev->mode.view.base = desc->view.base;
  dev->base8          = desc->view.base + (BYTES_PER_16BPP * desc->view.width * desc->view.height);
  ERR(set_base(dev, dev->mode.view.base, dev->base8));

  /* Only change mode if the new raster bitmap's characteristics differ from the current.
   *
   * Note: don't compare reduced blanking flag because if the rest is the same, we can use the
   * same blanking type. However, there's an outside chance that the low_blank hint was changed
   * since entering the current mode in which case you may well want a mode change to happen but
   * we'll just hope that never happens (seems like a very unlikely scenario).
   */
  if (desc->view.width  != dev->mode.view.width  ||
      desc->view.height != dev->mode.view.height ||
      desc->view.bpp    != dev->mode.view.bpp)
  {
    ERR(dlo_usb_chan_sel(dev, dlo_mode_data[mode].mode_en, dlo_mode_data[mode].mode_en_sz));
    ERR(dlo_usb_write_buf(dev, dlo_mode_data[mode].data, dlo_mode_data[mode].data_sz));
    ERR(dlo_usb_chan_sel(dev, DLO_MODE_POSTAMBLE, DSIZEOF(DLO_MODE_POSTAMBLE)));
  }

  /* Update the device with the new mode details */
  dev->mode         = *desc;
  dev->mode.refresh = dlo_mode_data[mode].refresh;
  dev->low_blank    = dlo_mode_data[mode].low_blank;

  //DPRINTF("mode: select: %ux%u @ %u Hz %u bpp (base &%X base8 &%X low? %s)\n",
  //        dev->mode.view.width, dev->mode.view.height, dev->mode.refresh, dev->mode.view.bpp,
  //        dev->mode.view.base, dev->base8, dev->low_blank ? "yes" : "no");

  /* Flush the command buffer */
  ERR(dlo_usb_write(dev));

  /* Return a warning for DL160 modes */
  return (mode < DLO_DL120_MODES) ? dlo_warn_dl160_mode : dlo_ok;
}


static uint16_t add_supported(dlo_device_t * const dev, uint16_t idx, const uint16_t width, const uint16_t height, const uint8_t refresh)
{
  dlo_modenum_t num;

  num = get_mode_number(dev, width, height, refresh, 24);
  if (num != DLO_INVALID_MODE)
    dev->supported[idx++] = num;

  //DPRINTF("mode: add_supt: %ux%u @ %u Hz, %u bpp = num %u\n", width, height, refresh, 24, (int)num);

  return idx;
}


static dlo_retcode_t lookup_edid_modes(dlo_device_t * const dev, const dlo_edid_t * const edid)
{
  uint32_t timings, bit;
  uint32_t idx    = 0;

  /* Add mode numbers for any supported established timing modes */
  timings = edid->est_timings.timings[0] | (edid->est_timings.timings[1] << 8) | (edid->est_timings.resvd << 16);
  for (bit = 0; bit < 24; bit++)
  {
    if (est_timings[bit].width)
      idx = add_supported(dev, idx, est_timings[bit].width, est_timings[bit].height, est_timings[bit].refresh);
  }

  /* Fill any remaining array entries with the invalid mode constant */
  while (idx < DLO_MODE_DATA_NUM - 1)
    dev->supported[idx++] = DLO_INVALID_MODE;

  return dlo_ok;
}


static dlo_retcode_t set_base(dlo_device_t * const dev, const dlo_ptr_t base, const dlo_ptr_t base8)
{
  //DPRINTF("mode: set_base: base=&%X base8=&%X\n", base, base8);
  ERR(vbuf(dev, WRITE_VIDREG_LOCK, DSIZEOF(WRITE_VIDREG_LOCK)));
  ERR(vreg(dev, 0x20, base >> 16));
  ERR(vreg(dev, 0x21, base >> 8));
  ERR(vreg(dev, 0x22, base));
  ERR(vreg(dev, 0x26, base8 >> 16));
  ERR(vreg(dev, 0x27, base8 >> 8));
  ERR(vreg(dev, 0x28, base8));
  ERR(vbuf(dev, WRITE_VIDREG_UNLOCK, DSIZEOF(WRITE_VIDREG_UNLOCK)));
  ERR(dlo_usb_write(dev));
  //DPRINTF("mode: set_base complete\n");

  return dlo_ok;
}


static bool bad_edid_checksum(const uint8_t * const ptr, const size_t size)
{
  uint32_t i;
  uint8_t  csum = 0;

  for (i = 0; i < size; i++)
    csum += ptr[i];

  return 0 != csum;
}

static dlo_retcode_t unpack_edid_detailed_timing(edid_detail_unpacked_t * const monitor, const uint8_t * const ptr)
{
  monitor->pixelClock10KHz = LETOHS(RD_S(ptr));
  if (monitor->pixelClock10KHz == 0)
    return dlo_warn_no_edid_detailed_timing;

  monitor->hActive         = ptr[0x02] + ((ptr[0x04] & 0xF0) << 4);
  monitor->hBlanking       = ptr[0x03] + ((ptr[0x04] & 0x0F) << 8);
  monitor->vActive         = ptr[0x05] + ((ptr[0x07] & 0xF0) << 4);
  monitor->vBlanking       = ptr[0x06] + ((ptr[0x07] & 0x0F) << 8);
  monitor->hSyncOffset     = ptr[0x08] + ((ptr[0x0B] & 0xC0) << 2);
  monitor->hSyncPulseWidth = ptr[0x09] + ((ptr[0x0B] & 0x30) << 4);
  monitor->vSyncOffset     = (ptr[0x0A] >> 4) + ((ptr[0x0B] & 0x0C) << 6);
  monitor->vSyncPulseWidth = (ptr[0x0A] & 0x0F) + ((ptr[0x0B] & 0x03) << 8);
  monitor->hImageSizeMm    = ptr[0x0C] + ((ptr[0x0E] & 0xF0) << 4);
  monitor->vImageSizeMm    = ptr[0x0D] + ((ptr[0x0E] & 0x0F) << 8);
  monitor->hBorder         = ptr[0x0F];
  monitor->vBorder         = ptr[0x10];
  monitor->bInterlaced     = (ptr[0x11] & 0x80) ? 1 : 0;
  monitor->bStereo         = (ptr[0x11] & 0x60) ? 1 : 0;
  monitor->bSeparateSync   = (ptr[0x11] & 0x18) ? 1 : 0;
  monitor->bVSyncPositive  = (ptr[0x11] & 0x04) ? 1 : 0;
  monitor->bHSyncPositive  = (ptr[0x11] & 0x02) ? 1 : 0;
  monitor->bStereoMode     = (ptr[0x11] & 0x01) ? 1 : 0;

  DPRINTF("edid: Pixel Clock:     %u KHz\n", monitor->pixelClock10KHz * 10);
  DPRINTF("edid: H Active pixels: %u\n", monitor->hActive);
  DPRINTF("edid: H Blank pixels:  %u\n", monitor->hBlanking);
  DPRINTF("edid: V Active pixels: %u\n", monitor->vActive);
  DPRINTF("edid: V Blank pixels:  %u\n", monitor->vBlanking);
  DPRINTF("edid: H Sync Offset:   %u\n", monitor->hSyncOffset);
  DPRINTF("edid: H Sync Width:    %u\n", monitor->hSyncPulseWidth);
  DPRINTF("edid: V Sync Offset:   %u\n", monitor->vSyncOffset);
  DPRINTF("edid: V Sync Width:    %u\n", monitor->vSyncPulseWidth);
  DPRINTF("edid: H size:          %u mm\n", monitor->hImageSizeMm);
  DPRINTF("edid: V size:          %u mm\n", monitor->vImageSizeMm);
  DPRINTF("edid: H Border         %u\n", monitor->hBorder);
  DPRINTF("edid: V Border         %u\n", monitor->vBorder);
  DPRINTF("edid: interlaced %u stereo %u separateSync %u\n", monitor->bInterlaced, monitor->bStereo, monitor->bSeparateSync);
  DPRINTF("edid: bVSyncPositive %u bHSyncPositive %u bStereoMode %u\n", monitor->bVSyncPositive, monitor->bHSyncPositive, monitor->bStereoMode);
  
  return dlo_ok;
}

static void parse_edid_colours(edid_colours_t * const cols, const uint8_t * const ptr)
{
  /* Read the raw data */
  cols->red_grn_low = RD_B(ptr + 0x19);
  cols->blu_wht_low = RD_B(ptr + 0x1A);
  cols->red_x       = RD_B(ptr + 0x1B);
  cols->red_y       = RD_B(ptr + 0x1C);
  cols->grn_x       = RD_B(ptr + 0x1D);
  cols->grn_y       = RD_B(ptr + 0x1E);
  cols->blu_x       = RD_B(ptr + 0x1F);
  cols->blu_y       = RD_B(ptr + 0x20);
  cols->wht_x       = RD_B(ptr + 0x21);
  cols->wht_y       = RD_B(ptr + 0x22);

  /* Do the expansion */
  cols->red_x = ((cols->red_grn_low & 0xC0) >> 6) + (cols->red_x << 2);
  cols->red_y = ((cols->red_grn_low & 0x30) >> 4) + (cols->red_y << 2);
  cols->grn_x = ((cols->red_grn_low & 0x0C) >> 2) + (cols->grn_x << 2);
  cols->grn_y = ((cols->red_grn_low & 0x03) >> 0) + (cols->grn_y << 2);
  cols->blu_x = ((cols->blu_wht_low & 0xC0) >> 6) + (cols->blu_x << 2);
  cols->blu_y = ((cols->blu_wht_low & 0x40) >> 4) + (cols->blu_y << 2);
  cols->wht_x = ((cols->blu_wht_low & 0x0C) >> 2) + (cols->wht_y << 2);
  cols->wht_y = ((cols->blu_wht_low & 0x03) >> 0) + (cols->wht_y << 2);
}


/* End of file -------------------------------------------------------------------------*/
