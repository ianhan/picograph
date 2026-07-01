/** @file dlo_usb.c
 *
 *  @brief Implements the USB-specific connectivity functions.
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
#include "dlo_usb.h"
#include "dlo_base.h"
#include "dlo_mode.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

/** If a tusb_ function call returns an error code, jump to the "error" label. */
#define TBERR_GOTO(cmd) do { if (!(cmd)) { err = dlo_err_usb; goto error; } } while(0)
#define TXERR_GOTO(cmd) do { if ((cmd) != XFER_RESULT_SUCCESS) { err = dlo_err_usb; goto error; } } while(0)

/* File-scope defines ------------------------------------------------------------------*/


#define NR_USB_REQUEST_STATUS_DW  (0x06)  /**< USB control message: request type. */
#define NR_USB_REQUEST_CHANNEL    (0x12)  /**< USB control message: request type. */
#define NR_USB_REQUEST_I2C_SUB_IO (0x02)  /**< USB control message: request type. */

/** USB VendorID for a DisplayLink device.
 */
#define VENDORID_DISPLAYLINK (0x17E9)

/** Number of milliseconds to wait before timing-out a USB control message.
 */
#define CTRL_TIMEOUT (100u)

/** Number of milliseconds to wait before timing-out channel selection message.
 */
#define CHANSEL_TIMEOUT (5000u)

/** Number of milliseconds to wait before timing-out a USB bulk transfer.
 */
#define WRITE_TIMEOUT (10000u)

/** Number of milliseconds to wait before timing-out a request for the device type.
 */
#define ID_TIMEOUT (1000u)

/** Largest full-speed bulk transfer that fits TinyUSB's uint16_t HCD length. */
#define WRITE_MAX_XFER_BYTES (1023u * 64u)

/** Number of ping-pong command buffers used to overlap encoding with bulk transfers. */
#define CMD_BUF_COUNT (2u)

/** Usable size of each ping-pong command buffer (halves of the BUF_SIZE allocation). */
#define CMD_BUF_BYTES (BUF_SIZE / CMD_BUF_COUNT)

#if CMD_BUF_BYTES <= DLO_USB_COMMAND_BUFFER_HIGH_WATER_MARK
#error "Each ping-pong command buffer must be larger than the high water mark"
#endif

/** Byte sequence to send to the device to select the default communication channel.
 */
#define STD_CHANNEL "\x57\xCD\xDC\xA7\x1C\x88\x5E\x15\x60\xFE\xC6\x97\x16\x3D\x47\xF2"


/* File-scope types --------------------------------------------------------------------*/


/* External scope variables ------------------------------------------------------------*/


int32_t usberr = 0;


/* File-scope variables ----------------------------------------------------------------*/


/** Pointer to a copy of the last error message string read out of libusb (or NULL).
 */
static char *usb_err_str = NULL;


/* File-scope function declarations ----------------------------------------------------*/


/** Attempt to read the EDID structure from the monitor attached to the specified device.
 *
 *  @param  dev    Device structure pointer.
 *
 *  @return  Return code, zero for no error.
 */
static dlo_retcode_t read_edid(dlo_device_t * const dev);


/** Spin until the device's bulk endpoint has no transfer in flight, servicing the USB stack. */
static void dlo_usb_wait_idle(const dlo_device_t * const dev);



/* Public function definitions ---------------------------------------------------------*/

// Invoked when device is mounted (configured)
__attribute__((weak)) void tuh_mount_cb (uint8_t daddr)
{
  static bool fInitialized = false;
  
  if (!fInitialized) {
    dlo_init_t ini_flags = { 0 };
    fInitialized = (dlo_init(ini_flags) == dlo_ok);
  }
  
  if (dlo_check_device(daddr) == dlo_ok) {
    dlo_claim_t cnf_flags = { 0 };
    dlo_dev_t uid = 0;
    uid = dlo_claim_first_device(cnf_flags, 0);
    if (uid) {
        dlo_fill_rect(uid, NULL, NULL, DLO_RGB(0, 0, 0));
        dlo_flush_usb(uid, true);
        if (dlo_device_configured)
            dlo_device_configured(uid);
    }
  }
}

// Invoked when device is unmounted (bus reset/unplugged)
__attribute__((weak)) void tuh_umount_cb(uint8_t daddr)
{
  //todo: removal
}

char *dlo_usb_strerror(void)
{
  //DPRINTF("usb: error lookup %d\n", usberr);
  return usb_err_str;
}


dlo_retcode_t dlo_usb_init(const dlo_init_t flags)
{
  /* Initialise libusb */
  //DPRINTF("usb: init\n");

  /* Add nodes onto the device list for any DisplayLink devices we find */
  //DPRINTF("usb: init: enum\n");

  return dlo_ok;
}


dlo_retcode_t dlo_usb_final(const dlo_final_t flags)
{
  if (usb_err_str)
    dlo_free(usb_err_str);

  return dlo_ok;
}

dlo_retcode_t dlo_usb_enumerate(const bool init)
{
  return dlo_ok;
}


static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len) {
    // TODO: Check for runover.
    (void)utf8_len;
    // Get the UTF-16 length out of the data itself.

    for (size_t i = 0; i < utf16_len; i++) {
        uint16_t chr = utf16[i];
        if (chr < 0x80) {
            *utf8++ = chr & 0xffu;
        } else if (chr < 0x800) {
            *utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        } else {
            // TODO: Verify surrogate.
            *utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t *buf, size_t len) {
    size_t total_bytes = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t chr = buf[i];
        if (chr < 0x80) {
            total_bytes += 1;
        } else if (chr < 0x800) {
            total_bytes += 2;
        } else {
            total_bytes += 3;
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
    return (int) total_bytes;
}

static uint8_t tuh_descriptor_get_serial_string_sync_utf8(uint8_t daddr, char *buf, size_t buf_len) {
    uint8_t err = tuh_descriptor_get_serial_string_sync(daddr, 0x409, buf, buf_len);
    if (err == XFER_RESULT_SUCCESS) {
        if ((buf[0] & 0xff) != 0) {
            size_t utf16_len = ((buf[0] & 0xff) - 2) / sizeof(uint16_t);
            size_t utf8_len = (size_t) _count_utf8_bytes((uint16_t*)(buf + 2), utf16_len);
            _convert_utf16le_to_utf8((uint16_t *) buf + 2, utf16_len, buf, buf_len);
            ((uint8_t*) buf)[utf8_len] = '\0';
        }
    }
    return err;
}

dlo_retcode_t dlo_check_device(uint8_t daddr)
{
  static char     string[255] = {0};
  dlo_retcode_t   err      = dlo_ok;
  dlo_device_t   *dev      = NULL;
  uint8_t         buf[4] = {0};
  dlo_devtype_t   type;

  uint16_t PID, VID;
  tuh_vid_pid_get(daddr, &VID, &PID);

  DPRINTF("usb: check: daddr &%X vendorID &%X\n", (int)daddr, VID);

  /* Reject devices that don't have the DisplayLink VendorID */
  if (VID != VENDORID_DISPLAYLINK)
  {
    return dlo_err_unsupported;
  }
  //DPRINTF("usb: check: get type\n");

  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type      = TUSB_REQ_TYPE_VENDOR,
      .direction = TUSB_DIR_IN
    },
    .bRequest = NR_USB_REQUEST_STATUS_DW,
    .wValue   = 0,
    .wIndex   = 0,
    .wLength  = sizeof(buf)
  };

  tuh_xfer_t xfer = {
    .daddr       = daddr,
    .ep_addr     = 0,
    .setup       = &request,
    .buffer      = buf,
    .complete_cb = NULL,
    .user_data   = 0
  };
  
  TBERR_GOTO(tuh_control_xfer(&xfer));
  TXERR_GOTO(xfer.result);

  /* Ask the device for some status information */
  //DPRINTF("usb: check: type buf[3] = &%X\n", buf[3]);

  /* Determine what type of device we are connected to */
  switch ((buf[3] >> 4) & 0xF)
  {
    case dlo_dev_base:
      type = dlo_dev_base;
      break;
    case dlo_dev_alex:
      type = dlo_dev_alex;
      break;
    default:
      if (buf[3] == dlo_dev_ollie)
        type = dlo_dev_ollie;
      else
        type = dlo_dev_unknown;
  }

  /* Read the device serial number as a string */
  TXERR_GOTO(tuh_descriptor_get_serial_string_sync_utf8(daddr, string, sizeof(string)));
  //DPRINTF("usb: check: type &%X serial '%s'\n", (int)type, string);
  /* See if this device is already in our device list */
  dev = dlo_device_lookup(string);
  if (dev)
  {
    /* Use this opportunity to update the USB device structure pointer, just in
     * case it has moved.
     */
    dev->cnct->udev = daddr;
    //DPRINTF("usb: check: already in list\n");
  }
  else
  {
    /* Add a new device to the device list */
    //DPRINTF("usb: check: create new device\n");
    dev = dlo_new_device(type, string);
    NERR_GOTO(dev);

    /* It's not. Create and initialise a new list node for the device */
    dev->cnct = (dlo_usb_dev_t *)dlo_malloc(sizeof(dlo_usb_dev_t));
    NERR_GOTO(dev->cnct);
    dev->cnct->udev = daddr;
    dev->cnct->uhand = 0;
  }
  //DPRINTF("usb: check: dlpp node &%X\n", (int)dev);

  return dlo_ok;

error:
  /* Free our dev->cnct USB information structure */
  if (dev)
  {
    if (dev->cnct)
      dlo_free(dev->cnct);
    dev->cnct = NULL;
  }

  return err;
}
uint16_t count_interface_total_len(tusb_desc_interface_t const* desc_itf, uint8_t itf_count, uint16_t max_len)
{
  uint8_t const* p_desc = (uint8_t const*) desc_itf;
  uint16_t len = 0;

  while (itf_count--)
  {
    // Next on interface desc
    len += tu_desc_len(desc_itf);
    p_desc = tu_desc_next(p_desc);

    while (len < max_len)
    {
      // return on IAD regardless of itf count
      if ( tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION ) return len;

      if ( (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) &&
           ((tusb_desc_interface_t const*) p_desc)->bAlternateSetting == 0 )
      {
        break;
      }

      len += tu_desc_len(p_desc);
      p_desc = tu_desc_next(p_desc);
    }
  }

  return len;
}

uint8_t open_bulk_endpoint(uint8_t daddr, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
  // len = interface + hid + n*endpoints
  uint16_t const drv_len = (uint16_t) (sizeof(tusb_desc_interface_t) + sizeof(tusb_hid_descriptor_hid_t) +
                                       desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));

  // corrupted descriptor
  if (max_len < drv_len) return 0;

  uint8_t const *p_desc = (uint8_t const *) desc_itf;

  // HID descriptor
  p_desc = tu_desc_next(p_desc);
  tusb_hid_descriptor_hid_t const *desc_hid = (tusb_hid_descriptor_hid_t const *) p_desc;
  //if(HID_DESC_TYPE_HID != desc_hid->bDescriptorType) return;

  // Endpoint descriptor
  p_desc = tu_desc_next(p_desc);
  tusb_desc_endpoint_t const * desc_ep = (tusb_desc_endpoint_t const *) p_desc;

  for(int i = 0; i < desc_itf->bNumEndpoints; i++)
  {
    if (TUSB_DESC_ENDPOINT != desc_ep->bDescriptorType) return 0;

    if(tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT)
    {
      // skip if failed to open endpoint
      if ( ! tuh_edpt_open(daddr, desc_ep) ) return 0;
      printf("Opened to [dev %u: ep %02x]\r\n", daddr, desc_ep->bEndpointAddress);
      return desc_ep->bEndpointAddress;
    }

    p_desc = tu_desc_next(p_desc);
    desc_ep = (tusb_desc_endpoint_t const *) p_desc;
  }
  return 0;
}

uint8_t parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg)
{
  uint8_t const* desc_end = ((uint8_t const*) desc_cfg) + tu_le16toh(desc_cfg->wTotalLength);
  uint8_t const* p_desc   = tu_desc_next(desc_cfg);
  uint8_t ret = 0;
  // parse each interfaces
  while( p_desc < desc_end )
  {
    uint8_t assoc_itf_count = 1;

    // Class will always starts with Interface Association (if any) and then Interface descriptor
    if ( TUSB_DESC_INTERFACE_ASSOCIATION == tu_desc_type(p_desc) )
    {
      tusb_desc_interface_assoc_t const * desc_iad = (tusb_desc_interface_assoc_t const *) p_desc;
      assoc_itf_count = desc_iad->bInterfaceCount;

      p_desc = tu_desc_next(p_desc); // next to Interface
    }

    // must be interface from now
    if( TUSB_DESC_INTERFACE != tu_desc_type(p_desc) ) return 0;
    tusb_desc_interface_t const* desc_itf = (tusb_desc_interface_t const*) p_desc;

    uint16_t const drv_len = count_interface_total_len(desc_itf, assoc_itf_count, (uint16_t) (desc_end-p_desc));

    // probably corrupted descriptor
    if(drv_len < sizeof(tusb_desc_interface_t)) return 0;

    if (desc_itf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC)
    {
        ret = open_bulk_endpoint(dev_addr, desc_itf, drv_len);
        break;
    }

    // next Interface or IAD descriptor
    p_desc += drv_len;
  }
  return ret;
}

dlo_retcode_t dlo_usb_open(dlo_device_t * const dev)
{
  dlo_retcode_t   err = dlo_ok;
  uint8_t daddr = dev->cnct->udev;
  uint16_t temp_buf[128];
  
  /* Use this opportunity to open endpoint 1 for bulk write */
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf)))
  {
    /* Use dev->cnct->uhand for the endpoint address */
    dev->cnct->uhand = parse_config_descriptor(daddr, (tusb_desc_configuration_t*) temp_buf);
  }
  else
  {
    return dlo_err_usb;
  }

  if (!dev->cnct->uhand)
  {
    return dlo_err_open;
  }

  /* Mark the device as claimed */
  dev->claimed = true;

  /* Allocate the ping-pong command buffers (two halves of one allocation) */
  if (!dev->buffers[0])
  {
    //DPRINTF("usb: open: alloc buffer...\n");
    dev->buffers[0] = dlo_malloc(BUF_SIZE);
    NERR(dev->buffers[0]);
    dev->buffers[1] = dev->buffers[0] + CMD_BUF_BYTES;
    dev->buf_index = 0;
    dev->buffer = dev->buffers[0];
    dev->bufptr = dev->buffer;
    dev->bufend = dev->buffer + CMD_BUF_BYTES;
  }
  //DPRINTF("usb: open: buffer &%X, &%X, &%X\n", (int)dev->buffer, (int)dev->bufptr, (int)dev->bufend);

  /* Use the default timeout if none was specified */
  if (!dev->timeout)
    dev->timeout = WRITE_TIMEOUT;
  //DPRINTF("usb: open: timeout %u ms\n", dev->timeout);

  /* Initialise the supported modes array for this device to include all our pre-defined modes */
  use_default_modes(dev);

  /* Attempt to read the EDID information, to refine the supported modes array contents */
  err = read_edid(dev);
#ifdef DEBUG
  if (err != dlo_ok)
    DPRINTF("usb: open: edid error %u '%s'\n", (int)err, dlo_strerror(err));
#endif

  return dlo_ok;
}


dlo_retcode_t dlo_usb_close(dlo_device_t * const dev)
{
  if (dev->claimed)
  {
    if (dev->buffers[0])
    {
      /* A transfer may still be reading the buffers; let it finish before freeing */
      if (dev->cnct && dev->cnct->uhand && tuh_mounted(dev->cnct->udev))
        dlo_usb_wait_idle(dev);
      dlo_free(dev->buffers[0]);
      dev->buffers[0] = NULL;
      dev->buffers[1] = NULL;
      dev->buf_index = 0;
      dev->buffer = NULL;
      dev->bufptr = NULL;
      dev->bufend = NULL;
    }
    dev->claimed = false;
  }
  return dlo_ok;
}


dlo_retcode_t dlo_usb_chan_sel(const dlo_device_t * const dev, const char * const buf, const size_t size)
{
  if (size) {
        /* A channel/key change must not land while a bulk transfer is on the wire */
        if (dev->cnct && dev->cnct->uhand)
          dlo_usb_wait_idle(dev);

        tusb_control_request_t const request = {
            .bmRequestType_bit = {
            .recipient = TUSB_REQ_RCPT_DEVICE,
            .type      = TUSB_REQ_TYPE_VENDOR,
            .direction = TUSB_DIR_OUT
            },
            .bRequest = NR_USB_REQUEST_CHANNEL,
            .wValue   = 0,
            .wIndex   = 0,
            .wLength  = size
        };

        tuh_xfer_t xfer = {
            .daddr       = dev->cnct->udev,
            .ep_addr     = 0,
            .setup       = &request,
            .buffer      = (void *)buf,
            .complete_cb = NULL,
            .user_data   = 0
        };
    
        tuh_control_xfer(&xfer);
    }
  return dlo_ok;
}


dlo_retcode_t dlo_usb_std_chan(const dlo_device_t * const dev)
{
  dlo_retcode_t err;

  ASSERT(strlen(STD_CHANNEL) == 16);
  err = dlo_usb_chan_sel(dev, STD_CHANNEL, DSIZEOF(STD_CHANNEL));

  return err;
}


void dlo_xfer_cb(tuh_xfer_t* xfer) {}

/** Spin until the device's bulk endpoint has no transfer in flight, servicing the USB stack. */
static void dlo_usb_wait_idle(const dlo_device_t * const dev)
{
  while (usbh_edpt_busy(dev->cnct->udev, dev->cnct->uhand)) {
    tuh_task();
  }
}

/** Submit a buffer as one or more bulk transfers.
 *
 *  Each submission first waits for the previous transfer to complete, so commands
 *  always reach the device in order. The final transfer is left in flight: the
 *  caller must not modify or free @a buf until the endpoint is idle again.
 */
static dlo_retcode_t dlo_usb_write_buf_async(dlo_device_t * const dev, char * buf, size_t size)
{
#ifdef DEBUG_DUMP
  static char     outfile[64];
  static uint32_t outnum = 0;
  FILE           *out    = NULL;
#endif

  if (!dev->claimed)
    return dlo_err_unclaimed;

  if (!size)
    return dlo_ok;

  while (size)
  {
    size_t num = size > WRITE_MAX_XFER_BYTES ? WRITE_MAX_XFER_BYTES : size;

#ifdef DEBUG_DUMP
    (void) snprintf(outfile, sizeof(outfile), "dump/%02X/bulk%03X.dat", outnum & 0xFF, outnum >> 8);
    outnum++;
    out = fopen(outfile, "wb");
    if (out)
    {
      (void) fwrite(buf, num, 1, out);
      (void) fclose(out);
      out = NULL;
    }
#endif

    /* The previous transfer (or previous chunk of this one) must finish first */
    dlo_usb_wait_idle(dev);

    tuh_xfer_t xfer =
    {
      .daddr       = dev->cnct->udev,
      .ep_addr     = dev->cnct->uhand,
      .buflen      = num,
      .buffer      = buf,
      .complete_cb = dlo_xfer_cb,
      .user_data   = 0
    };

    if (!tuh_edpt_xfer(&xfer))
      return dlo_err_usb;

    buf  += num;
    size -= num;
  }
  return dlo_ok;
}

dlo_retcode_t dlo_usb_write(dlo_device_t * const dev)
{
  if (!dev->buffer)
    return dlo_ok;

  size_t size = dev->bufptr - dev->buffer;
  dlo_retcode_t err = dlo_usb_write_buf_async(dev, dev->buffer, size);

  /* Flip to the other command buffer so encoding can continue while the
   * submitted buffer is still on the wire. On error nothing was left in
   * flight, so the current buffer is simply reset and reused.
   */
  if (err == dlo_ok && size)
    dev->buf_index ^= 1u;

  dev->buffer = dev->buffers[dev->buf_index];
  dev->bufptr = dev->buffer;
  dev->bufend = dev->buffer + CMD_BUF_BYTES;

  return err;
}

dlo_retcode_t dlo_usb_discard(dlo_device_t * const dev)
{
  if (!dev)
    return dlo_err_bad_device;

  dev->bufptr = dev->buffer;
  return dlo_ok;
}


dlo_retcode_t dlo_usb_write_buf(dlo_device_t * const dev, char * buf, size_t size)
{
#ifdef WRITE_BUF_BODGE
  /* If the buffer to write is fewer than 513 bytes in size, copy into 513 byte buffer and pad with zeros */
  if (size && size < WRITE_BUF_BODGE)
  {
    dlo_retcode_t err;
    uint32_t       rem = WRITE_BUF_BODGE - size;
    char          *cpy = dlo_malloc(WRITE_BUF_BODGE);

    NERR(cpy);
    dlo_memcpy(cpy, buf, size);
    dlo_memset(cpy + size, 0, rem);
    err = dlo_usb_write_buf(dev, cpy, size + rem);
    dlo_free(cpy);

    return err;
  }
#endif

  dlo_retcode_t err = dlo_usb_write_buf_async(dev, buf, size);

  /* The caller owns buf, so the transfer must complete before returning */
  if (err == dlo_ok && size)
    dlo_usb_wait_idle(dev);

  return err;
}


dlo_retcode_t dlo_flush_usb(const dlo_dev_t uid, bool force)
{
  dlo_device_t *dev = (dlo_device_t *)uid;
  dlo_retcode_t err = dlo_ok;
  if (dev->bufend - dev->bufptr < BUF_HIGH_WATER_MARK || force)
    err = dlo_usb_write(dev);
  return err;
}


/* File-scope function definitions -----------------------------------------------------*/


static dlo_retcode_t read_edid(dlo_device_t * const dev)
{
  dlo_retcode_t err;
  uint32_t      i;
  uint8_t       buf[2];
  uint8_t      *edid;

  /* Allocate a buffer to hold the EDID structure */
  edid = dlo_malloc(EDID_STRUCT_SZ);
  NERR(edid);

  /* Attempt to read the EDID structure from the device */
  for (i = 0; i < EDID_STRUCT_SZ; i++)
  {
    tusb_control_request_t const request = {
      .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_DEVICE,
      .type      = TUSB_REQ_TYPE_VENDOR,
      .direction = TUSB_DIR_IN
      },
      .bRequest = NR_USB_REQUEST_I2C_SUB_IO,
      .wValue   = i << 8,
      .wIndex   = 0xA1,
      .wLength  = sizeof(buf)
    };

    tuh_xfer_t xfer = {
      .daddr       = dev->cnct->udev,
      .ep_addr     = 0,
      .setup       = &request,
      .buffer      = buf,
      .complete_cb = NULL,
      .user_data   = 0
    };

    tuh_control_xfer(&xfer);

    if (buf[0])
      ERR_GOTO(dlo_err_iic_op);
    //DPRINTF("usb: edid[%u]=&%02X\n", i, buf[1]);
    edid[i] = buf[1];
  }

  /* Supply the prospective EDID structure to the parser */
  ERR_GOTO(dlo_mode_parse_edid(dev, edid, EDID_STRUCT_SZ));

error:
  dlo_free(edid);

  return err;
}


/* End of file -------------------------------------------------------------------------*/
