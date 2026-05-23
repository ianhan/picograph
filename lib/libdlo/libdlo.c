/** @file libdlo.c
 *
 *  @brief This file implements the high-level API for the DisplayLink libdlo library.
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

#include <string.h>
#include "dlo_defs.h"
#include "dlo_grfx.h"
#include "dlo_mode.h"
#include "dlo_usb.h"


/* File-scope defines ------------------------------------------------------------------*/


/** Structure to hold information about how a rectangle may have been clipped
 */
typedef struct clip_s
{
  uint32_t left;        /**< Number of pixels clipped from left edge. */
  uint32_t right;       /**< Number of pixels clipped from right edge. */
  uint32_t below;       /**< Number of pixels clipped from bottom edge. */
  uint32_t above;       /**< Number of pixels clipped from top edge. */
} clip_t;               /**< A struct @a clip_s. */


/** Return value from function to check source and destination viewports in the rectangle copy function.
 */
typedef enum
{
  vstat_good_view = 0,  /**< Viewports are both good, not overlapping. */
  vstat_good_overlap,   /**< Viewports are both the same. */
  vstat_bad_view,       /**< Something is wrong with one or other viewport. */
  vstat_bad_overlap     /**< Viewports overlap but are not identical. */
} vstat_t;


/* File-scope inline functions ---------------------------------------------------------*/


/* File-scope types --------------------------------------------------------------------*/


/* External-scope variables ------------------------------------------------------------*/


char err_file[1024] = { '\0' };


uint32_t err_line = 0;


/* File-scope variables ----------------------------------------------------------------*/


/** Pointer to the head node in the device list.
 */
static dlo_device_t *dev_list = NULL;


/** Flag is toggled at each enumeration to spot removed devices.
 */
static bool check_state = false;


/* File-scope function declarations ----------------------------------------------------*/


/** Check that the specified device (pointer) corresponds to a current list node.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  true if the device is in the device list, else false.
 *
 *  This is a less-than-perfect test because although the device structure may have
 *  been removed from the list, a new device structure may have been allocated into
 *  the same address at some later point.
 *
 *  Ideally, this library should hold structures which themselves point to a device
 *  structure and include a flag saying whether the pointer is still valid (which is
 *  cleared when the device structure is freed). The UID passed into dlo calls would
 *  be a pointer to this (small) structure. The (small) structures are leaked so that
 *  it is possible to spot stale pointers from the caller.
 */
static bool valid_device(const dlo_device_t * const dev);


/** Remove the specified device from the device list.
 *
 *  @param  dev  Pointer to @a dlo_device_t structure.
 *
 *  @return  Return code, zero for no error.
 *
 *  This call will delink the specified device structure from the device list
 *  and free any resources associated with the device (including "releasing"
 *  the device).
 *
 *  NOTE: the @a dev_list variable is updated as a side-effect of this function.
 */
static dlo_retcode_t remove_device(dlo_device_t *dev);


/** Initialise an area structure based upon a viewport and rectangle within it.
 *
 *  @param  dev   Pointer to @a dlo_device_t structure.
 *  @param  view  Pointer to the viewport structure (may be NULL).
 *  @param  rec   Pointer to a rectangle structure (may be NULL).
 *  @param  area  Pointer to area structure to initialise from @a view and @a rec.
 *  @param  clip  Pointer to list of offsets that the source rectangle was clipped by.
 *
 *  @return  true if the area still has extent, false if it was clipped to nothing.
 *
 *  Given a viewport and rectangle within that viewport, calculate the corresponding area
 *  structure by clipping the rectangle as required so that no part of it lies outside of
 *  the viewport.
 *
 *  Where the viewport is NULL on entry, the viewport for the current display screen is
 *  used. Where the rectangle is NULL on entry, the whole of the viewport is used.
 *
 *  This call also updates the contents of the @a clip structure as a side-effect to
 *  indicate how many pixels were clipped from each edge of the rectangle in order to
 *  arrive at the output area. This is useful if another rectangle needs to be clipped
 *  to match the output rectangle.
 */
static bool sanitise_view_rect(const dlo_device_t * const dev, const dlo_view_t * const view, const dlo_rect_t * const rec, dlo_area_t * const area, clip_t * const clip);


/** For the rectangle copy, check that overlapping rectangles have exactly the same viewport.
 *
 *  @param  dev        Pointer to @a dlo_device_t structure.
 *  @param  src_view   Pointer to source viewport structure.
 *  @param  dest_view  Pointer to destination viewport structure.
 *
 *  @return  true if they don't overlap, or do overlap and are identical. Else returns false.
 */
static vstat_t check_overlaps(const dlo_device_t * const dev, const dlo_view_t * const src_view, const dlo_view_t * const dest_view);


/* Public function definitions ---------------------------------------------------------*/


#if 0
static uint32_t total_alloc = 0;

void *my_malloc(size_t size)
{
  uint32_t *blk = malloc(size + sizeof(uint32_t));

  if (blk)
  {
    *blk         = size;
    total_alloc += size;
    blk++;
  }
  DPRINTF("alloc: malloc &%X is %u bytes (total %u)\n", (uint32_t)blk, size, total_alloc);

  return (void *)blk;
}


void *my_realloc(void *blk, size_t size)
{
  uint32_t *myblk = (uint32_t *)blk;

  if (myblk)
  {
    myblk--;
    if (size)
    {
      myblk = realloc(myblk, size + sizeof(uint32_t));
      if (myblk)
      {
        total_alloc -= *myblk;
        total_alloc += size;
        *myblk       = size;
        myblk++;
      }
    }
    else
    {
      /* Free a block */
      total_alloc -= *myblk;
      myblk        = realloc(myblk, 0);
    }
  }
  else
  {
    /* Claim a new block */
    myblk = realloc(NULL, size + sizeof(uint32_t));
    if (myblk)
    {
      *myblk       = size;
      total_alloc += size;
      myblk++;
    }
  }
  DPRINTF("alloc: realloc &%X to %u bytes (total %u)\n", (uint32_t)myblk, size, total_alloc);

  return (void *)myblk;
}


void my_free(void *blk)
{
  uint32_t *myblk = (uint32_t *)blk;

  myblk--;
  total_alloc -= *myblk;
  free(myblk);

  DPRINTF("alloc: free &%X (total %u)\n", (uint32_t)blk, total_alloc);
}


uint32_t my_used(void)
{
  return total_alloc;
}
#endif


const char *dlo_strerror(const dlo_retcode_t err)
{
  static char str[1024];
  char *msg;

  switch (err)
  {
    /* Success... */
    case dlo_ok: return "Successful";
    /* Errors... */
    case dlo_err_memory:       break;
    case dlo_err_bad_area:     return "An invalid area was specified (e.g. of zero width or height)";
    case dlo_err_bad_col:      return "Unsupported colour depth";
    case dlo_err_bad_device:   return "Unknown device, possibly disconnected or powered down";
    case dlo_err_bad_fbuf:     return "Null pointer passed as local bitmap data";
    case dlo_err_bad_fmt:      return "Unsupported bitmap pixel format";
    case dlo_err_bad_mode:     return "Call to dlo_set_mode() failed due to unsupported mode parameters";
    case dlo_err_bad_view:     return "Invalid viewport specified (is screen mode set up?)";
    case dlo_err_big_scrape:   return "Bitmap is too wide for copy buffer";
    case dlo_err_buf_full:     return "Command buffer is full";
    case dlo_err_claimed:      return "Device is already claimed";
    case dlo_err_edid_fail:    return "Attempt to access EDID information failed";
    case dlo_err_iic_op:       return "IIC operation with device failed";
    case dlo_err_not_root:     return "Executable should be run as root (e.g. using 'su root' or 'sudo')";
    case dlo_err_open:         return "Attempt to open connection to device failed";
    case dlo_err_overlap:      return "Source and destination viewports cannot overlap (unless the same)";
    case dlo_err_reenum:       return "Reenumeration required before device can be claimed";
    case dlo_err_unclaimed:    return "Device cannot be written to: unclaimed";
    case dlo_err_unsupported:  return "Requested feature is not supported";
    case dlo_err_usb:          return dlo_usb_strerror();
    /* Warnings... */
    case dlo_warn_dl160_mode:  return "This screen mode may not display correctly on DL120 devices";
    default:                   return "Unknown error";
  }

  /* Memory allocation errors should have more information associated with them */
  msg = "A memory claim operation failed; out of memory";
  if (err_file[0])
    snprintf(&str[0], sizeof(str), "%s:%u: %s", &err_file[0], err_line, msg);
  else
    snprintf(&str[0], sizeof(str), "%s", msg);
  err_file[0] = '\0';
  err_line    = 0;
  return str;
}


dlo_retcode_t dlo_init(const dlo_init_t flags)
{
  /* Initialise the graphics primitives */
  DPRINTF("dlo: grfx_init\n");
  ERR(dlo_grfx_init(flags));

  /* Initialise the screen mode timings array */
  DPRINTF("dlo: mode_init\n");
  ERR(dlo_mode_init(flags));

  /* Initialise the USB comms code and function pointer table */
  DPRINTF("dlo: usb_init\n");
  ERR(dlo_usb_init(flags));

  return dlo_ok;
}


dlo_retcode_t dlo_final(const dlo_final_t flags)
{
  /* Free all claimed structures */
  while (dev_list)
  {
    (void) remove_device(dev_list);
  }

  ERR(dlo_grfx_final(flags));
  ERR(dlo_mode_final(flags));
  ERR(dlo_usb_final(flags));

  return dlo_ok;
}


dlo_devlist_t *dlo_enumerate_devices(void)
{
  dlo_devlist_t *out = NULL;
  dlo_device_t  *dev;
  dlo_retcode_t  err;
  uint32_t       num;

  // check_state = !check_state;

  /* Check USB for all DisplayLink devices that we can find. If there is an error returned,
   * then it is likely that the devlist could become out of step with reality. For that
   * reason, after any error, we throw away all unclaimed devices from the devlist to try to
   * minimise the chances.
   */
  //DPRINTF("dlo: enum: enumerating USB devices\n");
  ERR_GOTO(dlo_usb_enumerate(false));

  /* Remove all devices which weren't updated or added during this enumeration and
   * build the list of device information to return. Note: if a dlo_malloc() call
   * fails during this operation, we note it and continue otherwise the dev_list could
   * get out of step with reality.
   */
  dev = dev_list;
  out = NULL;
  num = 0;
  while (dev)
  {
    dlo_device_t *next = dev->next;

    if (dev->check == check_state)
    {
      dlo_devlist_t *tmp = (dlo_devlist_t *)dlo_malloc(sizeof(dlo_devlist_t));

      /* Add a node to the output list */
      if (tmp)
      {
        tmp->next        = out;
        tmp->dev.uid     = (dlo_dev_t)dev;
        tmp->dev.serial  = dev->serial;
        tmp->dev.type    = dev->type;
        tmp->dev.claimed = dev->claimed;
        out              = tmp;
        num++;
      }
      else
        err = dlo_err_memory;
    }
    else
    {
      // DPRINTF("dlo: enum: removed device &%X (%s)\n", (int)dev, dev->serial);
      (void) remove_device(dev);
    }
    dev = next;
  }

  /* Return pointer to output list if operation was successful */
  if (!err)
  {
    // DPRINTF("dlo: enum: return list of %u entries\n", num);
    return out;
  }

error:
  DPRINTF("dlo: enum: error &%X (usberr &%X)\n", err, usberr);

  /* Throw the output list away */
  while (out)
  {
    dlo_devlist_t *next = out->next;

    dlo_free(out);
    out = next;
  }

  /* Remove all unclaimed devices from our list */
  dev = dev_list;
  while (dev)
  {
    dlo_device_t *next = dev->next;

    if (!dev->claimed)
      (void) remove_device(dev);
    dev = next;
  }
  return NULL;
}


dlo_dev_t dlo_claim_device(const dlo_dev_t uid, const dlo_claim_t flags, const uint32_t timeout)
{
  dlo_device_t *dev = (dlo_device_t *)uid;
  dlo_retcode_t err;

  if (!dev)
    ERR_GOTO(dlo_err_bad_device);

  /* Confirm that the device structure pointer still exists */
  if (!valid_device(dev))
    ERR_GOTO(dlo_err_bad_device);

  /* Return error if device has already been claimed */
  if (dev->claimed)
    ERR_GOTO(dlo_err_claimed);

  dev->timeout = timeout;

  /* Attempt to open a connection to the device */
  err = dlo_usb_open(dev);
  while (err == dlo_err_reenum)
  {
    DPRINTF("dlo_usb_open failed with dlo_err_reenum. Retry\n");

    /* If the USB bus devices have changed, do the enumeration again */
    dlo_devlist_t *out = dlo_enumerate_devices();

    /* Throw the output list away */
    while (out)
    {
      dlo_devlist_t *next = out->next;

      dlo_free(out);
      out = next;
    }

    /* Confirm that the device structure pointer still exists */
    if (!valid_device(dev))
      ERR_GOTO(dlo_err_bad_device);

    /* Now try opening the connection again */
    err = dlo_usb_open(dev);
  }
  /* Any other errors from opening the connection get returned to the caller */
  ERR_GOTO(err);

  /* Attempt to change mode into the native resolution of the display (if we have one) */
  dlo_mode_set_default(dev, 0);

  return uid;

error:
  DPRINTF("dlo: claim: error %u '%s'\n", (int)err, dlo_strerror(err));

  return (dlo_dev_t)0;
}


static const char* parse_cmdline(int *argc_p, char *argv[])
{
  /* Scan the command line looking for --dlo:display.  Return the value found, if
   * any, else return NULL.  Remove all --dlo:* options from argv.
   */

  const char *display = NULL;
  int n;
  int m = 1;

  if (!argc_p) {
    return NULL;
  }

  for (n=1; n<*argc_p; ++n) {
    const char* arg = argv[n];
    if (strncmp(arg,"--dlo:",6)==0) {
      const char* dlo_arg = arg + 6;
      if (strncmp(dlo_arg,"display=",8)==0) {
        display = dlo_arg + 8;
      }
    } else {
      argv[m] = arg;
      ++m;
    }
  }
  *argc_p = m;
  return display;
}


dlo_dev_t dlo_claim_first_device(const dlo_claim_t flags, const uint32_t timeout)
{
  dlo_devlist_t *node;
  dlo_devlist_t *next;
  dlo_dev_t      uid = 0;

  /* Look for a DisplayLink device to connect to - note the first one which is unclaimed */
  node = dlo_enumerate_devices();
  
  // dlo_enumerate_devices allocates memory for each node, which we must free
  while (node)
  {
    dlo_device_t *dev = (dlo_device_t *)node->dev.uid;

    if (!uid && !dev->claimed)
    {
      uid = dlo_claim_device(node->dev.uid, flags, timeout);
    }

    /* If we haven't claimed a device, move on to the next one */
    next = node->next;
    dlo_free(node);
    node = next;
  }
  return uid;
}


dlo_dev_t dlo_claim_default_device(int *argc_p, char *argv[],
                                   const dlo_claim_t flags, const uint32_t timeout)
{
  const char *display = parse_cmdline(argc_p, argv);
  if (!display) {
    display = getenv("DLODISPLAY");
  }

  // For now, assume that display is simply the serial number.  Some thought about
  // exactly what its format should be is needed.
  const char *serial = display;

  dlo_devlist_t *node;
  dlo_devlist_t *next;
  dlo_dev_t      uid = 0;

  /* Look for a DisplayLink device to connect to - note the first one which is unclaimed */
  node = dlo_enumerate_devices();
  
  // dlo_enumerate_devices allocates memory for each node, which we must free
  while (node)
  {
    dlo_device_t *dev = (dlo_device_t *)node->dev.uid;

    if (!uid && !dev->claimed
        && (!serial || strcmp(serial,dev->serial)==0))
    {
      uid = dlo_claim_device(node->dev.uid, flags, timeout);
    }

    /* If we haven't claimed a device, move on to the next one */
    next = node->next;
    dlo_free(node);
    node = next;
  }
  return uid;
}


dlo_retcode_t dlo_release_device(const dlo_dev_t uid)
{
  dlo_device_t *dev = (dlo_device_t *)uid;

  return dev ? dlo_usb_close(dev) : dlo_err_bad_device;
}


dlo_devinfo_t *dlo_device_info(const dlo_dev_t uid)
{
  static dlo_devinfo_t info;
  dlo_device_t        *dev = (dlo_device_t *)uid;

  if (!dev)
    return NULL;

  info.uid     = uid;
  info.serial  = dev->serial;
  info.type    = dev->type;
  info.claimed = dev->claimed;

  return &info;
}


dlo_retcode_t dlo_set_mode(const dlo_dev_t uid, const dlo_mode_t * const desc)
{
  dlo_device_t *dev = (dlo_device_t *)uid;
  dlo_modenum_t num;

  if (!dev)
    return dlo_err_bad_device;

  /* TODO: clean up the cases we use the direct-from-EDID path, when we 
   * generally clean up (or is that clean out?) the standard mode tables
   */
  if ((desc == NULL) || (desc->view.width == 0) ||
      /* or if width and hight the same as monitor's preferred mode */
      ((dev->edid.timings[0].pixelClock10KHz) && (desc->view.width == dev->edid.timings[0].hActive) && (desc->view.height == dev->edid.timings[0].vActive))) {

    uint32_t base = 0;

    if (desc) {
      base = desc->view.base;
    }

    /* Then do a modeset directly from the preferred mode in the EDID */
    return dlo_mode_set_default(dev, base);
  }

  DPRINTF("dlo: set_mode: asking for width %u height %u refresh %u bpp %u\n", desc->view.width, desc->view.height, desc->refresh, desc->view.bpp);

  /* See if we can provide a mode which matches the required parameters */
  num = dlo_mode_lookup(dev, desc->view.width, desc->view.height, desc->refresh, desc->view.bpp);
  DPRINTF("dlo: set_mode: lookup %d\n", (int32_t)num);

  return num == DLO_INVALID_MODE ? dlo_err_bad_mode : dlo_mode_change(dev, desc, num);
}


dlo_mode_t *dlo_get_mode(const dlo_dev_t uid)
{
  dlo_device_t *dev = (dlo_device_t *)uid;

  return dev ? &(dev->mode) : NULL;
}


dlo_retcode_t dlo_fill_rect(const dlo_dev_t uid, const dlo_view_t * const view, const dlo_rect_t * const rec, const dlo_col32_t col)
{
  static clip_t        clip;
  static dlo_area_t    area;
  dlo_device_t * const dev = (dlo_device_t *)uid;

  if (!dev)
    return dlo_err_bad_device;

  /* Clip the rectangle to its viewport edges */
  if (!sanitise_view_rect(dev, view, rec, &area, &clip))
    return dlo_ok;

  return dlo_grfx_fill_rect(dev, &area, col);
}


dlo_retcode_t dlo_copy_rect(const dlo_dev_t uid,
                             const dlo_view_t * const src_view,  const dlo_rect_t * const src_rec,
                             const dlo_view_t * const dest_view, const dlo_dot_t * const dest_pos)
{
  static clip_t        clip;
  static dlo_area_t    src_area;
  static dlo_area_t    dest_area;
  static dlo_rect_t    dest_rec;
  dlo_device_t * const dev     = (dlo_device_t *)uid;
  bool                 overlap = false;

  if (!dev)
    return dlo_err_bad_device;

  /* Check to see if (and how) the source and destination viewports overlap */
  switch (check_overlaps(dev, src_view, dest_view))
  {
    case vstat_good_view:
      break;
    case vstat_bad_view:
      return dlo_err_bad_view;
    case vstat_bad_overlap:
      return dlo_err_overlap;
    default:
    {
      DPRINTF("dlo: copy_rec: src_view == dest_view... overlap? ");
      overlap = (dest_pos->x + src_rec->width  > src_rec->origin.x) &&
                (dest_pos->y + src_rec->height > src_rec->origin.y) &&
                (dest_pos->x < src_rec->origin.x + src_rec->width)  &&
                (dest_pos->y < src_rec->origin.y + src_rec->height);
      DPRINTF("%s\n", overlap ? "yes" : "no");
    }
  }

  /* Clip the source rectangle to its viewport edges */
  if (!sanitise_view_rect(dev, src_view, src_rec, &src_area, &clip))
    return dlo_ok;

  /* Clip the destination rectangle by the same amounts as we needed to clip the source */
  dest_rec.origin.x = dest_pos ? dest_pos->x + clip.left  : clip.left;
  dest_rec.origin.y = dest_pos ? dest_pos->y + clip.below : clip.below;
  dest_rec.width    = src_area.view.width;
  dest_rec.height   = src_area.view.height;

  /* Further clip the destination rectangle to its viewport edges */
  if (!sanitise_view_rect(dev, dest_view, &dest_rec, &dest_area, &clip))
    return dlo_ok;

  /* Further clip the source area by the same amounts as we needed to clip the destination */
  src_area.view.base  += (BYTES_PER_16BPP * clip.left) + (BYTES_PER_16BPP * clip.below * src_area.stride);
  src_area.base8      += (BYTES_PER_8BPP  * clip.left) + (BYTES_PER_8BPP  * clip.below * src_area.stride);
  src_area.view.width  = dest_area.view.width;
  src_area.view.height = dest_area.view.height;

  return dlo_grfx_copy_rect(dev, &src_area, &dest_area, overlap);
}


dlo_retcode_t dlo_copy_host_bmp(const dlo_dev_t uid, const dlo_bmpflags_t flags,
                             const dlo_fbuf_t * const fbuf,
                             const dlo_view_t * const dest_view, const dlo_dot_t * const dest_pos)
{
  static clip_t        clip;
  static dlo_area_t    dest_area;
  static dlo_rect_t    dest_rec;
  static dlo_fbuf_t    src_fbuf;
  dlo_device_t * const dev = (dlo_device_t *)uid;
  uint32_t             off;

  /* Do some sanity checks */
  if (!dev)
    return dlo_err_bad_device;

  if (!fbuf)
    return dlo_err_bad_fbuf;

  if (!fbuf->width || !fbuf->height)
    return dlo_ok;

  /* Clip the destination rectangle to its viewport edges */
  src_fbuf          = *fbuf;
  dest_rec.origin.x = dest_pos ? dest_pos->x : 0;
  dest_rec.origin.y = dest_pos ? dest_pos->y : 0;
  dest_rec.width    = src_fbuf.width;
  dest_rec.height   = src_fbuf.height;
  if (!sanitise_view_rect(dev, dest_view, &dest_rec, &dest_area, &clip))
    return dlo_ok;

  /* Update the source framebuffer information if the destination was clipped */
  off              = clip.left + (clip.below * src_fbuf.stride);
  off             *= FORMAT_TO_BYTES_PER_PIXEL(src_fbuf.fmt);
  src_fbuf.base    = (void *)((unsigned long)src_fbuf.base + (unsigned long)off);
  src_fbuf.width  -= clip.left  + clip.right;
  src_fbuf.height -= clip.below + clip.above;

  return dlo_grfx_copy_host_bmp(dev, flags, &src_fbuf, &dest_area);
}


dlo_device_t *dlo_new_device(const dlo_devtype_t type, const char * const serial)
{
  dlo_device_t *dev = (dlo_device_t *)dlo_malloc(sizeof(dlo_device_t));
  char          *str = NULL;
  dlo_retcode_t err;

  NERR_GOTO(dev);

  /* Copy the serial number string and keep a pointer to it */
  str = dlo_malloc(1 + strlen(serial));
  NERR_GOTO(str);
  dev->serial = str;
  strcpy(dev->serial, serial);

  /* Link into the device list structure */
  dev->prev    = NULL;
  dev->next    = dev_list;
  dev_list     = dev;
  dev->claimed = false;

  /* Defaults are for an "Alex" device via USB */
  dev->type    = type;
  dev->check   = check_state;
  dev->timeout = 0;

  /* Mode-dependent attributes and function pointers */
  dev->mode.view.width  = 0;
  dev->mode.view.height = 0;
  dev->mode.view.bpp    = 0;
  dev->mode.refresh     = 0;
  dev->mode.view.base   = 0;
  dev->base8            = 0;
  dev->low_blank        = false;

  /* Device-dependent attributes */
  dev->buffer = NULL;

  /* Connection-dependent attributes.
   *
   * It is up to the communications code (re)initialise this values to something which
   * makes sense to it.
   */
  dev->cnct = NULL;

  /* Set up some feature flags based upon what we know about DisplayLink device types.
   *
   * NOTE: this section will probably become more refined as more features are added
   * to libdlo. Currently, it assumes all devices are the same.
   */
  dev->memory  = 16 * 1024 * 1024;

  DPRINTF("dlo: new_device: initialised &%X\n", (int)dev);

  return dev;

error:
  if (str)
    dlo_free(str);
  if (dev)
    dlo_free(dev);

  return NULL;
}


dlo_device_t *dlo_device_lookup(const char * const serial)
{
  dlo_device_t *dev = dev_list;

  //DPRINTF("dlo: lookup: '%s' dev_list &%X\n", serial, (int)dev_list);

  while (dev)
  {
    //DPRINTF("dlo: lookup: dev &%X serial '%s'\n", (int)dev, dev->serial);
    if (0 == strcmp(dev->serial, serial))
    {
      dev->check = check_state;
      return dev;
    }
    dev = dev->next;
  }
  return NULL;
}


/* File-scope function definitions -----------------------------------------------------*/


static bool valid_device(const dlo_device_t * const dev)
{
  dlo_device_t *node = dev_list;

  while (node)
  {
    if (node == dev)
      return true;
    node = node->next;
  }
  return false;
}


static dlo_retcode_t remove_device(dlo_device_t *dev)
{
  dlo_retcode_t err;
  dlo_device_t *prev = dev->prev;
  dlo_device_t *next = dev->next;

  /* Delink the device from the device list */
  if (prev)
    prev->next = next;
  else
    dev_list = next;
  if (next)
    next->prev = prev;

  /* Disconnect from the device (if connected) */
  if (dev->claimed)
    err = dlo_usb_close(dev);
  else
    err = dlo_ok;

  /* Free the structure (and associated data) even if there was an error */
  if (dev->serial)
    dlo_free(dev->serial);
  dlo_free(dev);

  return err;
}


static bool sanitise_view_rect(const dlo_device_t * const dev, const dlo_view_t * const view, const dlo_rect_t * const rec, dlo_area_t * const area, clip_t * const clip)
{
  static dlo_rect_t        my_rec;
  const dlo_view_t * const my_view = view ? view : &(dev->mode.view);
  uint32_t                 pix_off;

  /* Exit here if my_view is still NULL (i.e. no screen mode is set up yet) */
  if (!my_view)
    return false;

  /* Default the rectangle to the whole of the the view, if none was specified */
  if (rec)
    my_rec = *rec;
  else
  {
    my_rec.origin.x = 0;
    my_rec.origin.y = 0;
    my_rec.width    = my_view->width;
    my_rec.height   = my_view->height;
  }

  /* Exit here if the source rectangle is never going to appear in the viewport */
  if (my_rec.origin.x + (int32_t)my_rec.width < 0 || my_rec.origin.y + (int32_t)my_rec.height < 0)
    return false;
  if (my_rec.origin.x >= (int32_t)my_view->width || my_rec.origin.y >= (int32_t)my_view->height)
    return false;

  /* Initialise the clip structure to empty */
  clip->left  = 0;
  clip->right = 0;
  clip->below = 0;
  clip->above = 0;

  /* Now clip the rectangle to lie within the viewport */
  if (my_rec.origin.x + (int32_t)my_rec.width > (int32_t)my_view->width)
  {
    clip->right  = my_rec.width - (my_view->width - my_rec.origin.x);
    my_rec.width = my_view->width - my_rec.origin.x;
  }
  if (my_rec.origin.y + (int32_t)my_rec.height > (int32_t)my_view->height)
  {
    clip->above   = my_rec.height - (my_view->height - my_rec.origin.y);
    my_rec.height = my_view->height - my_rec.origin.y;
  }
  if (my_rec.origin.x < 0)
  {
    clip->left      = -my_rec.origin.x;
    my_rec.width   += my_rec.origin.x;
    my_rec.origin.x = 0;
  }
  if (my_rec.origin.y < 0)
  {
    clip->below     = -my_rec.origin.y;
    my_rec.height  += my_rec.origin.y;
    my_rec.origin.y = 0;
  }

  /* Exit here if we've clipped the rectangle away to nothing */
  if (!my_rec.width || !my_rec.height)
    return false;

  /* Create the clipped area structure */
  pix_off           = my_rec.origin.x + (my_rec.origin.y * my_view->width);
  area->view.width  = my_rec.width;
  area->view.height = my_rec.height;
  area->view.bpp    = my_view->bpp;
  area->view.base   = my_view->base + (BYTES_PER_16BPP * pix_off);
  area->base8       = my_view->base + (BYTES_PER_16BPP * my_view->width * my_view->height) + pix_off;
  area->stride      = my_view->width;

  return true;
}


static vstat_t check_overlaps(const dlo_device_t * const dev, const dlo_view_t * const src_view, const dlo_view_t * const dest_view)
{
  const dlo_view_t * const src  = src_view  ? src_view  : &(dev->mode.view);
  const dlo_view_t * const dest = dest_view ? dest_view : &(dev->mode.view);
  dlo_ptr_t                src_end;
  dlo_ptr_t                dest_end;

  /* If no display mode is set up, we should stop here to avoid null dereferences */
  if (!src || !dest)
    return vstat_bad_view;

  /* If base addresses are the same, the other attributes must also be the same */
  if (src->base == dest->base)
  {
    return (src->width  == dest->width &&
            src->height == dest->height &&
            src->bpp    == dest->bpp) ? vstat_good_overlap : vstat_bad_overlap;
  }

  /* Ensure we're happy with the source bits per pixel */
  if (src->bpp != 16 && src->bpp != 24)
    return vstat_bad_view;

  /* Ensure we're happy with the destination bits per pixel */
  if (dest->bpp != 16 && dest->bpp != 24)
    return vstat_bad_view;

  /* Compute the end addresses of both views */
  src_end  = src->base  + (BYTES_PER_16BPP * src->width  * src->height);
  dest_end = dest->base + (BYTES_PER_16BPP * dest->width * dest->height);
  if (src->bpp == 24)
    src_end += BYTES_PER_8BPP * src->width * src->height;
  if (dest->bpp == 24)
    dest_end += BYTES_PER_8BPP * dest->width * dest->height;

  /* Any overlap in address range means we have a problem */
  return (dest_end <= src->base || dest->base >= src_end) ? vstat_good_view : vstat_bad_overlap;
}


/* End of file -------------------------------------------------------------------------*/
