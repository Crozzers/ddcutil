/* libusb_reports.c
 *
 * Report libusb data structures
 *
 * <copyright>
 * Copyright (C) 2016 Sanford Rockowitz <rockowitz@minsoft.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * </endcopyright>
 *
 * Portions adapted from lsusb.c (command lsusb) by Thomas Sailer and David Brownell
 */

// Adapted from usbplay2 file libusb_util.c

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>

#include "util/string_util.h"
#include "util/report_util.h"
#include "util/device_id_util.h"

#include "usb_util/base_hid_report_descriptor.h"
#include "usb_util/hid_report_descriptor.h"
#include "usb_util/usb_hid_common.h"

#include "usb_util/libusb_reports.h"

//
// Identifier to name tables
//

#define VALUE_NAME(v) {v,#v}
#define VALUE_NAME_END {0xff,NULL}

#define VN2(v,t) {v,#v,t}
#define VN_END2 {0xff,NULL,NULL}

typedef struct {
   Byte   value;
   char * name;
} Value_Name;

typedef struct {
   Byte   value;
   char * name;
   char * title;
} Value_Name_Title;

Value_Name class_id_table[] = {
      {LIBUSB_CLASS_PER_INTERFACE, "LIBUSB_CLASS_PER_INTERFACE"},
      VALUE_NAME(LIBUSB_CLASS_AUDIO),
      {0xff,NULL},
      VALUE_NAME_END
};

Value_Name_Title class_code_table[] = {
      /** In the context of a \ref libusb_device_descriptor "device descriptor",
       * LIBUSB_CLASS_PER_INSTANCE indicates that each interface specifies its
       * own class information and all interfaces operate independently.
       */
      VN2( LIBUSB_CLASS_PER_INTERFACE,       "Per interface"),          // 0
      VN2( LIBUSB_CLASS_AUDIO,               "Audio"),                  // 1
      VN2( LIBUSB_CLASS_COMM,                "Communications"),         // 2
      VN2( LIBUSB_CLASS_HID,                 "Human Interface Device"), // 3
      VN2( LIBUSB_CLASS_PHYSICAL,            "Physical device"),        // 5
      VN2( LIBUSB_CLASS_PRINTER,             "Printer"),                // 7
      VN2( LIBUSB_CLASS_IMAGE,               "Image"),                  // 6
      VN2( LIBUSB_CLASS_MASS_STORAGE,        "Mass storage"),           // 8
      VN2( LIBUSB_CLASS_HUB,                 "Hub"),                    // 9
      VN2( LIBUSB_CLASS_DATA,                "Data"),                   // 10
      VN2( LIBUSB_CLASS_SMART_CARD,          "Smart card"),             // 0x0b
      VN2( LIBUSB_CLASS_CONTENT_SECURITY,    "Content security"),       // 0x0d
      VN2( LIBUSB_CLASS_VIDEO,               "Video"),                  // 0x0e
      VN2( LIBUSB_CLASS_PERSONAL_HEALTHCARE, "Personal healthcare"),    // 0x0f
      VN2( LIBUSB_CLASS_DIAGNOSTIC_DEVICE,   "Diagnostic device"),      // 0xdc
      VN2( LIBUSB_CLASS_WIRELESS,            "Wireless"),               // 0xe0
      VN2( LIBUSB_CLASS_APPLICATION,         "Application"),            // 0xfe
      VN2( LIBUSB_CLASS_VENDOR_SPEC,         "Vendor specific"),        // 0xff
      VN_END2
};

Value_Name_Title descriptor_type_table[] = {
      VN2( LIBUSB_DT_DEVICE,                "Device"),            // 0x01
      VN2( LIBUSB_DT_CONFIG,                "Configuration"),     // 0x02
      VN2( LIBUSB_DT_STRING,                "String"),            // 0x03
      VN2( LIBUSB_DT_INTERFACE,             "Interface"),         // 0x04
      VN2( LIBUSB_DT_ENDPOINT,              "Endpoint"),          // 0x05
      VN2( LIBUSB_DT_BOS,                   "BOS" ),              // 0x0f,
      VN2( LIBUSB_DT_DEVICE_CAPABILITY,     "Device Capability"), // 0x10,
      VN2( LIBUSB_DT_HID,                   "HID"),               // 0x21,
      VN2( LIBUSB_DT_REPORT,                "HID report"),        // 0x22,
      VN2( LIBUSB_DT_PHYSICAL,              "Physical"),          // 0x23,
      VN2( LIBUSB_DT_HUB,                   "Hub"),               // 0x29,
      VN2( LIBUSB_DT_SUPERSPEED_HUB,        "SuperSpeed Hub"),    // 0x2a,
      VN2( LIBUSB_DT_SS_ENDPOINT_COMPANION, "SuperSpeed Endpoint Companion"),  // 0x30
      VN_END2
};


#ifdef REF
/** \ingroup desc
 * Endpoint direction. Values for bit 7 of the
 * \ref libusb_endpoint_descriptor::bEndpointAddress "endpoint address" scheme.
 */
enum libusb_endpoint_direction {
   /** In: device-to-host */
   LIBUSB_ENDPOINT_IN = 0x80,

   /** Out: host-to-device */
   LIBUSB_ENDPOINT_OUT = 0x00
};
#endif


Value_Name_Title endpoint_direction_table[] = {
      VN2( LIBUSB_ENDPOINT_IN, "IN"),
      VN2( LIBUSB_ENDPOINT_OUT, "OUT"),
      VN_END2
      };

#ifdef REF
#define LIBUSB_TRANSFER_TYPE_MASK         0x03    /* in bmAttributes */

/** \ingroup desc
 * Endpoint transfer type. Values for bits 0:1 of the
 * \ref libusb_endpoint_descriptor::bmAttributes "endpoint attributes" field.
 */
enum libusb_transfer_type {
   /** Control endpoint */
   LIBUSB_TRANSFER_TYPE_CONTROL = 0,

   /** Isochronous endpoint */
   LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1,

   /** Bulk endpoint */
   LIBUSB_TRANSFER_TYPE_BULK = 2,

   /** Interrupt endpoint */
   LIBUSB_TRANSFER_TYPE_INTERRUPT = 3,

   /** Stream endpoint */
   LIBUSB_TRANSFER_TYPE_BULK_STREAM = 4,
};

#endif


Value_Name_Title transfer_type_table[] = {
      VN2(LIBUSB_TRANSFER_TYPE_CONTROL,     "Control"),          // 0
      VN2(LIBUSB_TRANSFER_TYPE_ISOCHRONOUS, "Isochronous"),      // 1
      VN2(LIBUSB_TRANSFER_TYPE_BULK,        "Bulk"),             // 2
      VN2(LIBUSB_TRANSFER_TYPE_INTERRUPT,   "Interrupt"),        // 3
      VN2(LIBUSB_TRANSFER_TYPE_BULK_STREAM, "Bulk Stream"),      // 4
      VN_END2
};



char * vn_title(Value_Name_Title* table, Byte val) {
   char * result = NULL;

   Value_Name_Title * cur = table;
   for (; cur->name; cur++) {
      if (val == cur->value) {
         result = cur->title;
         break;
      }
   }
   return result;
}

char * descriptor_title(Byte val) {
   return vn_title(descriptor_type_table, val);
}

char * endpoint_direction_title(Byte val) {
   return vn_title(endpoint_direction_table, val);
}

char * transfer_type_title(Byte val) {
   return vn_title(transfer_type_table, val);
}

char * class_code_title(Byte val) {
   return vn_title(class_code_table, val);
}


//
// Misc Utilities
//

#define LIBUSB_STRING_BUFFER_SIZE 100
char libusb_string_buffer[LIBUSB_STRING_BUFFER_SIZE];

char * lookup_libusb_string(struct libusb_device_handle * dh, int string_id) {
   int rc = libusb_get_string_descriptor_ascii(
               dh,
               string_id,
               (unsigned char *) libusb_string_buffer,
               LIBUSB_STRING_BUFFER_SIZE);
   if (rc < 0) {
        REPORT_LIBUSB_ERROR("libusb_get_string_descriptor_ascii",  rc, LIBUSB_CONTINUE);
        strcpy(libusb_string_buffer, "<Unknown string>");
   }
   else {
        assert(rc == strlen(libusb_string_buffer));

   }
   return (char *) libusb_string_buffer;
}


wchar_t libusb_string_buffer_wide[LIBUSB_STRING_BUFFER_SIZE];

wchar_t * lookup_libusb_string_wide(struct libusb_device_handle * dh, int string_id) {
   int rc = libusb_get_string_descriptor(
               dh,
               string_id,
               33,         // US English
               (unsigned char *) libusb_string_buffer_wide,  // N. CAST
               LIBUSB_STRING_BUFFER_SIZE);
   if (rc < 0) {
        REPORT_LIBUSB_ERROR("libusb_get_string_descriptor_wide",  rc, LIBUSB_CONTINUE);
        wcscpy(libusb_string_buffer_wide, L"<Unknown string>");
   }
   else {
      // printf("(%s) rc=%d, wcslen(libusb_string_buffer_wide)=%d, strlen=%d\n",
      //       __func__, rc, wcslen(libusb_string_buffer_wide), strlen(libusb_string_buffer_wide) );
      // assert(rc == wcslen(libusb_string_buffer));

   }
   return (wchar_t *) libusb_string_buffer;
}



// from lsusb.c
/* ---------------------------------------------------------------------- */

/* workaround libusb API goofs:  "byte" should never be sign extended;
 * using "char" is trouble.  Likewise, sizes should never be negative.
 */

static inline int
typesafe_control_msg(
      libusb_device_handle * dev,
      unsigned char          requesttype,
      unsigned char          request,
      int                    value,
      int                    idx,
      unsigned char *        bytes,
      unsigned               size,
      int                    timeout)
{
   int ret = libusb_control_transfer(
                dev, requesttype, request, value, idx, bytes, size, timeout);

   if (ret < 0)
      return -ret;
   else
      return ret;
}

#define usb_control_msg    typesafe_control_msg


//
// Report functions for libusb data structures
//

void report_endpoint_descriptor(
        const struct libusb_endpoint_descriptor * epdesc,
        libusb_device_handle *                    dh,    // may be null
        int                                       depth)
{
   int d1 = depth+1;
   rpt_structure_loc("libusb_endpoint_descriptor", epdesc, depth);

   rpt_vstring(d1, "%-20s 0x%02x  (%s)",
                   "bDescriptorType:",
                   epdesc->bDescriptorType,
                   descriptor_title(epdesc->bDescriptorType)
              );

   /** bEndpointAddress: The address of the endpoint described by this descriptor. Bits 0:3 are
    * the endpoint number. Bits 4:6 are reserved. Bit 7 indicates direction,
    * see \ref libusb_endpoint_direction.
    */
   unsigned char endpoint_number = epdesc->bEndpointAddress & 0x0f;
   char * direction_name = (epdesc->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ? "IN" : "OUT";
   rpt_vstring(d1, "%-20s 0x%02x  Endpoint number: %d  Direction: %s",
               "bEndpointAddress:",
               epdesc->bEndpointAddress,
               endpoint_number,
               direction_name);

   /** Attributes which apply to the endpoint when it is configured using
    * the bConfigurationValue. Bits 0:1 determine the transfer type and
    * correspond to \ref libusb_transfer_type. Bits 2:3 are only used for
    * isochronous endpoints and correspond to \ref libusb_iso_sync_type.
    * Bits 4:5 are also only used for isochronous endpoints and correspond to
    * \ref libusb_iso_usage_type. Bits 6:7 are reserved.
    */
   // uint8_t  bmAttributes;
   Byte transfer_type = epdesc->bmAttributes & 0x03;
   // Byte isoc_sync_type     = epdesc->bmAttributes & 0x0c;     // unused
   // Byte isoc_iso_usage_type = epdesc->bmAttributes & 0x30;    // unused
   rpt_vstring(d1, "%-20s 0x%02x  Transfer Type: %s",
                   "bmAttributes:",
                   epdesc->bmAttributes,
                   transfer_type_title(transfer_type)
                   );

   /** Maximum packet size this endpoint is capable of sending/receiving. */
   // uint16_t wMaxPacketSize;
   rpt_vstring(d1, "%-20s %u",
                   "wMaxPacketSize:",
                   epdesc->wMaxPacketSize);

   /** Interval for polling endpoint for data transfers. */
   // uint8_t  bInterval;
   rpt_vstring(d1, "%-20s %d    %s",
                   "bInterval",
                   epdesc->bInterval,
                   "(data transfer polling interval)"
                  );

   // skipping several

   /** Length of the extra descriptors, in bytes. */
   // int extra_length;
   rpt_vstring(d1, "%-20s %d     (length of extra descriptors)",
                   "extra_length:",
                   epdesc->extra_length);
}


// from lsusb.c



 /* Get bytes of HID Report Descriptor
  *
  * Arguments:
  *   dh
  *   bInterfaceNumber
  *   rptlen
  *   dbuf
  *   dbufsz
  *
  * Returns:        true if success, false if not
  */
 bool get_raw_report_descriptor(
         struct libusb_device_handle * dh,
         uint8_t                       bInterfaceNumber,
         uint16_t                      rptlen,        // report length
         Byte *                        dbuf,
         int                           dbufsz,
         int *                         pbytes_read)
{
    bool ok = false;
    assert(dh);
#define CTRL_RETRIES  2
#define CTRL_TIMEOUT (5*1000) /* milliseconds */

    int bytes_read = 0;

    if (rptlen > dbufsz) {
       printf("report descriptor too long\n");
       return false;
    }

    if (libusb_claim_interface(dh, bInterfaceNumber) == 0) {
       int retries = 4;
       bytes_read = 0;
       while (bytes_read < rptlen && retries--) {
          bytes_read = usb_control_msg(
                          dh,
                          LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_INTERFACE,
                          LIBUSB_REQUEST_GET_DESCRIPTOR,
                          (LIBUSB_DT_REPORT << 8),
                          bInterfaceNumber,
                          dbuf,
                          rptlen,
                          CTRL_TIMEOUT);
       }
       if (bytes_read > 0) {
          if (bytes_read < rptlen)
             printf("          Warning: incomplete report descriptor\n");
          // dump_report_desc(dbuf, bytes_read);    // old way

          *pbytes_read = bytes_read;


          ok = true;
       }
       libusb_release_interface(dh, bInterfaceNumber);
    }
    else {
       // recent Linuxes require claim() for RECIP_INTERFACE,
       // so "rmmod hid" will often make these available.
       printf("         Report Descriptors: \n"
              "           ** UNAVAILABLE **\n");
       ok = false;
    }
    return ok;
}


 /* Reports struct libusb_interface_descriptor
  *
  * Arguments:
  *   inter      pointer to libusb_interface_descriptor instance
  *   dh         display handle, not required but allows for additional information
  *   depth      logical indentation depth
  *
  * Returns:     nothing
  */
void report_libusb_interface_descriptor(
        const struct libusb_interface_descriptor * inter,
        libusb_device_handle *                     dh,    // may be null
        int                                        depth)
{
   int d1 = depth+1;

   rpt_structure_loc("libusb_interface_descriptor", inter, depth);

   /** Size of this descriptor (in bytes) */
   // uint8_t  bLength;
   rpt_vstring(d1, "%-20s %d", "bLength", inter->bLength);

   /** Descriptor type. Will have value
    * \ref libusb_descriptor_type::LIBUSB_DT_INTERFACE LIBUSB_DT_INTERFACE
    * in this context. */
   // uint8_t  bDescriptorType;
   // rpt_int("bDescriptorType", NULL, inter->bDescriptorType, d1);
   rpt_vstring(d1, "%-20s 0x%02x  %s",
                   "bDescriptorType:",
                   inter->bDescriptorType,
                   descriptor_title(inter->bDescriptorType)
              );

   /** Number of this interface */
   // uint8_t  bInterfaceNumber;
   // rpt_int("bInterfaceNumber", NULL, inter->bInterfaceNumber, d1);
   rpt_vstring(d1, "%-20s %u",
                   "bInterfaceNumber:",
                   inter->bInterfaceNumber);


   /** Value used to select this alternate setting for this interface */
   // uint8_t  bAlternateSetting;
   // rpt_int("bAlternateSetting", NULL, inter->bAlternateSetting, d1);
   rpt_vstring(d1, "%-20s %u", "bAlternateSetting:", inter->bAlternateSetting);


   /** Number of endpoints used by this interface (excluding the control
    * endpoint). */
   // uint8_t  bNumEndpoints;
   // rpt_int("bNumEndpoints", "excludes control endpoint", inter->bNumEndpoints, d1);
   rpt_vstring(d1, "%-20s %u", "bNumEndpoints:", inter->bNumEndpoints);

   /** USB-IF class code for this interface. See \ref libusb_class_code. */
   // uint8_t  bInterfaceClass;
   // rpt_int("bInterfaceClass", NULL, inter->bInterfaceClass, d1);
   // rpt_vstring(d1, "bInterfaceClass:       0x%02x (%d)", inter->bInterfaceClass, inter->bInterfaceClass);
   rpt_vstring(d1, "%-20s %u  (0x%02x)  %s",
                   "bInterfaceClass:",
                   inter->bInterfaceClass,
                   inter->bInterfaceClass,
                   class_code_title(inter->bInterfaceClass) );

   /** USB-IF subclass code for this interface, qualified by the
    * bInterfaceClass value */
   // uint8_t  bInterfaceSubClass;
   // rpt_int("bInterfaceSubClass", NULL, inter->bInterfaceSubClass, d1);
   // rpt_vstring(d1, "bInterfaceSubClass:       0x%02x (%d)", inter->bInterfaceSubClass, inter->bInterfaceSubClass);
   rpt_vstring(d1, "%-20s %u  (0x%02x)  %s",
                   "bInterfaceSubClass:",
                   inter->bInterfaceSubClass,
                   inter->bInterfaceSubClass,
                   "");

   /** USB-IF protocol code for this interface, qualified by the
    * bInterfaceClass and bInterfaceSubClass values */
   // uint8_t  bInterfaceProtocol;
   // rpt_int("bInterfaceProtocol", NULL, inter->bInterfaceProtocol, d1);
   rpt_vstring(d1, "%-20s %u  (0x%02x)  %s",
                   "bInterfaceProtocol:",
                   inter->bInterfaceProtocol,
                   inter->bInterfaceProtocol,
                   "");

   // Index of string descriptor describing this interface: uint8_t  iInterface;
   char * interface_name = "";
   if (dh && inter->iInterface > 0)
      interface_name = lookup_libusb_string(dh, inter->iInterface);
   rpt_vstring(d1, "%-20s %d  \"%s\" ",
                   "iInterface",
                   inter->iInterface,
                   interface_name
                   );


   /** Array of endpoint descriptors. This length of this array is determined
    * by the bNumEndpoints field. */
   // const struct libusb_endpoint_descriptor *endpoint;
   // NB: This is an array of endpoint descriptors,
   //     not an array of pointers to endpoint descriptors
   //     nor a pointer to array of pointers to endpoint descriptors
   int ndx = 0;
   for (ndx=0; ndx<inter->bNumEndpoints; ndx++) {
      const struct libusb_endpoint_descriptor *epdesc =
         epdesc = &(inter->endpoint[ndx]);
      report_endpoint_descriptor(epdesc, dh, d1);
   }

   /** Extra descriptors. If libusb encounters unknown interface descriptors,
    * it will store them here, should you wish to parse them. */
   // const unsigned char *extra;

   /** Length of the extra descriptors, in bytes. */
   // int extra_length;
   // rpt_int("extra_length", "len of extra descriptors", inter->extra_length, d1);
   rpt_vstring(d1, "%-20s %d     (length of extra descriptors)",
                   "extra_length:",
                   inter->extra_length);
   if (inter->extra_length > 0) {
      rpt_vstring(d1, "extra_data at %p: ", inter->extra);
      rpt_hex_dump(inter->extra, inter->extra_length, d1);

      if (dh) {
      if (inter->bInterfaceClass == LIBUSB_CLASS_HID) {  // 3
         const Byte * cur_extra = inter->extra;
         int remaining_length = inter->extra_length;
         while (remaining_length > 0) {
            HID_Descriptor * cur_hid_desc = (HID_Descriptor *) cur_extra;
            assert(cur_hid_desc->bLength <= remaining_length);
            report_hid_descriptor(dh, inter->bInterfaceNumber, cur_hid_desc, d1);

            cur_extra += cur_hid_desc->bLength;
            remaining_length -= cur_hid_desc->bLength;
         }
      }
      }

   }
}


/* Reports struct libusb_interface
 *
 * Arguments:
 *   inter      pointer to libusb_interface instance
 *   dh         display handle, not required but allows for additional information
 *   depth      logical indentation depth
 *
 * Returns:     nothing
 *
 * struct libusb_interface represents a collection of alternate settings for a
 * particular USB interface.  It contains an array of interface descriptors, one
 * for each alternate settings.
 */
void report_libusb_interface(
      const struct libusb_interface *  interface,
      libusb_device_handle *           dh,    // may be null
      int                              depth)
{
   int d1 = depth+1;
   rpt_structure_loc("libusb_interface", interface, depth);

   // The number of alternate settings that belong to this interface
   rpt_vstring(d1, "%-20s", "num_altsetting", interface->num_altsetting);
   // rpt_int("num_altsetting", NULL, interface->num_altsetting, d1);

   for (int ndx=0; ndx<interface->num_altsetting; ndx++) {
      report_libusb_interface_descriptor(&interface->altsetting[ndx], dh, d1);
   }
}


/* Reports struct libusb_config_descriptor
 *
 * Arguments:
 *   config     pointer to libusb_config_descriptor instance
 *   dh         display handle, not required but allows for additional information
 *   depth      logical indentation depth
 *
 * Returns:     nothing
 *
 * struct libusb_config_descriptor represents the standard USB configuration
 * descriptor. This descriptor is documented in section 9.6.3 of the USB 3.0
 * specification.  It contains multiple libusb_interface structs.
 *
 * All multiple-byte fields are represented in host-endian format.
 */
void report_libusb_config_descriptor(
        const struct libusb_config_descriptor * config,
        libusb_device_handle *                  dh,    // may be null
        int                                     depth)
{
   int d1 = depth+1;

   rpt_structure_loc("libusb_config_descriptor", config, depth);

   // Size of this descriptor (in bytes): uint8_t  bLength;
   rpt_vstring(d1, "%-20s  %d", "bLength:", config->bLength, d1);


   // Descriptor type. Will have value LIBUSB_DT_CONFIG in this context.
   rpt_vstring(d1, "%-20s 0x%02x  %s",
                   "bDescriptorType:",
                   config->bDescriptorType,              // uint8_t  bDescriptorType;
                   descriptor_title(config->bDescriptorType)
              );

   /** Total length of data returned for this configuration */
   //uint16_t wTotalLength;

   /** Number of interfaces supported by this configuration */
   // uint8_t  bNumInterfaces;
   rpt_int("bNumInterfaces", NULL, config->bNumInterfaces, d1);

   /** Identifier value for this configuration */
   // uint8_t  bConfigurationValue;
   rpt_int("bConfigurationValue", "id for this configuration", config->bConfigurationValue, d1);

   /** Index of string descriptor describing this configuration */
   // uint8_t  iConfiguration;
   rpt_int("iConfiguration", "index of string descriptor", config->iConfiguration, d1);

   /** Configuration characteristics */
   // uint8_t  bmAttributes;
   rpt_uint8_as_hex("bmAttributes", "config characteristics", config->bmAttributes, d1);

   /** Maximum power consumption of the USB device from this bus in this
    * configuration when the device is fully opreation. Expressed in units
    * of 2 mA. */
   // uint8_t  MaxPower;
   rpt_int("MaxPower", "units of 2 mA", config->MaxPower, d1);

   /** Array of interfaces supported by this configuration. The length of
    * this array is determined by the bNumInterfaces field. */
   // const struct libusb_interface *interface;
   int ndx = 0;
   for (ndx=0; ndx<config->bNumInterfaces; ndx++) {
      const struct libusb_interface *inter = &(config->interface[ndx]);
      report_libusb_interface(inter, dh, d1);
   }


   /** Extra descriptors. If libusb encounters unknown configuration
    * descriptors, it will store them here, should you wish to parse them. */
   // const unsigned char *extra;

   /** Length of the extra descriptors, in bytes. */
   // int extra_length;
   rpt_int("extra_length", "len of extra descriptors", config->extra_length, d1);

}


/* Reports struct libusb_device_descriptor.
 *
 * Arguments:
 *    desc             pointer to libusb_device_descriptor instance
 *    dh               if non-null, string values are looked up for string descriptor indexes
 *    depth            logical indentation depth
 *
 * Returns:    nothing
 *
 * struct libusb_device_descriptor represents the standard USB device descriptor.
 * This descriptor is documented in section 9.6.1 of the USB 3.0 specification.
 *
 * All multiple-byte fields are represented in host-endian format.
 */
void report_libusb_device_descriptor(
        const struct libusb_device_descriptor * desc,
        libusb_device_handle *                  dh,    // may be null
        int                                     depth)
{
   int d1 = depth+1;

   rpt_structure_loc("libusb_device_descriptor", desc, depth);

   // Size of this descriptor (in bytes):  uint8_t  bLength;
   rpt_vstring(d1, "%-20s %d", "bLength:", desc->bLength);

   // Descriptor type. Will have value LIBUSB_DT_DEVICE in this context.
   rpt_vstring(d1, "%-20s 0x%02x  %s",
                   "bDescriptorType:",
                   desc->bDescriptorType,          // uint8_t  bDescriptorType;
                   descriptor_title(desc->bDescriptorType) );

   /** USB specification release number in binary-coded decimal. A value of
    * 0x0200 indicates USB 2.0, 0x0110 indicates USB 1.1, etc. */
   // uint16_t bcdUSB;
   unsigned int bcdHi  = desc->bcdUSB >> 8;
   unsigned int bcdLo  = desc->bcdUSB & 0x0f;
   rpt_vstring(d1,"%-20s 0x%04x (%x.%02x)",
                  "bcdUSB",
                  desc->bcdUSB,
                  bcdHi,
                  bcdLo);

   /** USB-IF class code for the device. See \ref libusb_class_code. */
   rpt_vstring(d1, "%-20s 0x%02x  (%u)  %s",
                   "bDeviceClass:",
                   desc->bDeviceClass,             // uint8_t  bDeviceClass;
                   desc->bDeviceClass,
                   class_code_title(desc->bDeviceClass) );

   /** USB-IF subclass code for the device, qualified by the bDeviceClass value */
   // uint8_t  bDeviceSubClass;
   rpt_vstring(d1, "%-20s 0x%02x (%u)", "bDeviceSubClass:",
                   desc->bDeviceSubClass, desc->bDeviceSubClass);

   /** USB-IF protocol code for the device, qualified by the bDeviceClass and
    * bDeviceSubClass values */
   // uint8_t  bDeviceProtocol;
   // rpt_int("bDeviceProtocol", NULL, desc->bDeviceProtocol, d1);
   rpt_vstring(d1, "%-20s 0x%02x (%u)", "bDeviceProtocol:", desc->bDeviceProtocol, desc->bDeviceProtocol);

   /** Maximum packet size for endpoint 0 */
   // uint8_t  bMaxPacketSize0;
   rpt_vstring(d1, "%-20s %u  (max size for endpoint 0)", "bMaxPacketSize0:", desc->bMaxPacketSize0);

   Pci_Usb_Id_Names usb_id_names =
            devid_get_usb_names(
                      desc->idVendor,
                      desc->idProduct,
                      0,
                      2);

   // USB-IF vendor ID:  uint16_t idVendor;
   rpt_vstring(d1, "%-20s 0x%04x  %s", "idVendor:", desc->idVendor, usb_id_names.vendor_name);

   // USB-IF product ID:  uint16_t idProduct;
   rpt_vstring(d1, "%-20s 0x%04x  %s", "idProduct:", desc->idProduct, usb_id_names.device_name);

   // Device release number in binary-coded decimal: uint16_t bcdDevice;
   bcdHi  = desc->bcdDevice >> 8;
   bcdLo  = desc->bcdDevice & 0x0f;
   rpt_vstring(d1, "%-20s %2x.%02x  (device release number)", "bcdDevice:", bcdHi, bcdLo);


   // Index of string descriptor describing manufacturer: uint8_t  iManufacturer;
   // rpt_vstring(d1, "%-20s %u  (mfg string descriptor index)", "iManufacturer:", desc->iManufacturer);

   char *    mfg_name = "";
   // wchar_t * mfg_name_w = L"";

   if (dh && desc->iManufacturer) {
      mfg_name = lookup_libusb_string(dh, desc->iManufacturer);
      // mfg_name_w =  lookup_libusb_string_wide(dh, desc->iManufacturer) ;
      // wprintf(L"Manufacturer (wide) %d -%ls\n",
      //          desc->iManufacturer,
      //            lookup_libusb_string_wide(dh, desc->iManufacturer) );
   }
   rpt_vstring(d1, "%-20s %d  %s", "iManufacturer:", desc->iManufacturer, mfg_name);
   // rpt_vstring(d1, "%-20s %u  %S", "iManufacturer:", desc->iManufacturer, mfg_name_w);


   // Index of string descriptor describing product: uint8_t  iProduct;
   // rpt_int("iProduct", "product string descriptor index", desc->iProduct, d1);

   char *    product_name = "";
   if (dh && desc->iProduct)
      product_name = lookup_libusb_string(dh, desc->iProduct);
   rpt_vstring(d1, "%-20s %u  %s", "iProduct:", desc->iProduct, product_name);


   //Index of string descriptor containing device serial number: uint8_t  iSerialNumber;
   // rpt_int("iSerialNumber", "index of string desc for serial num", desc->iProduct, d1);

   char *    sn_name = "";
   if (dh && desc->iSerialNumber)
      sn_name = lookup_libusb_string(dh, desc->iSerialNumber);
   rpt_vstring(d1, "%-20s %u  %s", "iSerialNumber:", desc->iSerialNumber, sn_name);



   // Number of possible configurations:  uint8_t  bNumConfigurations;
   rpt_vstring(d1, "%-20s %u (number of possible configurations)", "bNumConfigurations:", desc->bNumConfigurations);
}


char * format_port_number_path(unsigned char path[], int portct, char * buf, int bufsz) {
   *buf = 0;
   int ndx;

   for (ndx=0; ndx < portct; ndx++) {
      char *end = buf + strlen(buf);
      // printf("end=%p\n", end);
      if (ndx == 0)
         sprintf(end, "%u", path[ndx]);
      else
         sprintf(end, ".%u", path[ndx]);
    }
    return buf;
}


bool is_hub_descriptor(const struct libusb_device_descriptor * desc) {
   return (desc->bDeviceClass == 9);
}

#ifdef IN_PROGRESS
void report_open_dev(
      libusb_device *         dev,
      libusb_device_handle *  dh,    // must not be null
      bool                    show_hubs,
      int                     depth)
{
   bool debug = true;
   if (debug)
      printf("(%s) Starting.  dev=%p, dh=%p, show_hubs=%s\n", __func__, dev, dh, bool_repr(show_hubs));

   assert(dev);
   assert(dh);

   int d1 = depth+1;
   int rc;



}
#endif



/* Reports a single libusb device.
 *
 */
void report_libusb_device(
      libusb_device *         dev,
      bool                    show_hubs,
      int                     depth)
{
   bool debug = false;
   if (debug)
      printf("(%s) Starting. dev=%p, show_hubs=%s\n", __func__, dev, bool_repr(show_hubs));

   int d1 = depth+1;
   int rc;
   // int j;

   // if (debug) {
   rpt_structure_loc("libusb_device", dev, depth);
   uint8_t busno = libusb_get_bus_number(dev);
   uint8_t devno = libusb_get_device_address(dev);

   rpt_vstring(d1, "%-20s: %d  (0x%04x)", "Bus number",     busno, busno);
   rpt_vstring(d1, "%-20s: %d  (0x%04x)", "Device address", devno, devno);
   // }
   // else {
   //    rpt_vstring(depth, "USB bus:device = %d:%d", libusb_get_bus_number(dev), libusb_get_device_address(dev));
   // }
   uint8_t portno = libusb_get_port_number(dev);
   rpt_vstring(d1, "%-20s: %u (%s)", "Port number",
                   portno,
                   "libusb_get_port_number(), number of the port this device is connected to");

   /* uint8_t */ unsigned char path[8];
   int portct = libusb_get_port_numbers(dev, path, sizeof(path));
   char buf[100];
   format_port_number_path(path, portct, buf, 100);
   rpt_vstring(d1, "%-20s: %s (list of all port numbers from root)", "Port numbers", buf);

   struct libusb_device_descriptor desc;
   // copies data into struct pointed to by desc, does not allocate:
   rc = libusb_get_device_descriptor(dev, &desc);
   CHECK_LIBUSB_RC("libusb_get_device_descriptor", rc, LIBUSB_EXIT);

   if ( !show_hubs && is_hub_descriptor(&desc)) {
      rpt_title("Is hub device, skipping detail", d1);
   }
   else {

      struct libusb_device_handle * dh = NULL;
      int rc = libusb_open(dev, &dh);
      if (rc < 0) {
         REPORT_LIBUSB_ERROR("libusb_open", rc, LIBUSB_CONTINUE);
         dh = NULL;   // belt and suspenders
      }
      else {
         if (debug)
            printf("(%s) Successfully opened\n", __func__);
         int has_detach_kernel_capability =
               libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
         if (debug)
            printf("(%s) %s kernel detach driver capability\n",
                   __func__,
                   (has_detach_kernel_capability) ? "Has" : "Does not have");

         if (has_detach_kernel_capability) {
            rc = libusb_set_auto_detach_kernel_driver(dh, 1);
            if (rc < 0) {
               REPORT_LIBUSB_ERROR("libusb_set_auto_detach_kernel_driver", rc, LIBUSB_CONTINUE);
            }
         }

      }

#ifdef TEMPORARY_TEST
      if (dh) {
      // printf("String 0:\n");
      // printf("%s\n", lookup_libusb_string(dh, 0));    // INVALID_PARM
      printf("String 1:\n");
      printf("%s\n", lookup_libusb_string(dh, 1));
      }
#endif

      report_libusb_device_descriptor(&desc, dh, d1);

      struct libusb_config_descriptor *config;
      libusb_get_config_descriptor(dev, 0 /* config_index */, &config);  // returns a pointer
      report_libusb_config_descriptor(config, dh, d1);
      libusb_free_config_descriptor(config);

      if (dh)
         libusb_close(dh);
   }
   printf("\n");
   if (debug)
      printf("(%s) Done\n", __func__);
}


// Report a list of libusb_devices
void report_libusb_devices(libusb_device **devs, bool show_hubs, int depth)
{
      libusb_device *dev;

      int i = 0;
      while ((dev = devs[i++]) != NULL) {
         puts("");
         report_libusb_device(dev,  show_hubs, depth);
      }
}



#ifdef REF
typedef struct hid_class_descriptor {
   uint8_t     bDescriptorType;
   uint16_t    wDescriptorLength;
} HID_Class_Descriptor;

typedef struct hid_descriptor {
   uint8_t      bLength;
   uint8_t      bDescriptorType;
   uint16_t     bcdHID;
   uint8_t      bCountryCode;
   uint8_t      bNumDescriptors;    // number of class descriptors, always at least 1, i.e. Report descriptor
   uint8_t      bDescriptorType;    // start of first class descriptor
   uint16_t     wDescriptorLength;
} HID_Descriptor;
#endif




void report_hid_descriptor(
        libusb_device_handle * dh,
        uint8_t                bInterfaceNumber,
        HID_Descriptor *       desc,
        int                    depth)
{
   bool debug = false;
   if (debug)
      printf("(%s) Starting. dh=%p, bInterfaceNumber=%d, desc=%p\n",
            __func__, dh, bInterfaceNumber, desc);
   int d1 = depth+1;
   int d2 = depth+2;

   rpt_structure_loc("HID_Descriptor", desc, depth);

   rpt_vstring(d1, "%-20s   %u", "bLength", desc->bLength);
   rpt_vstring(d1, "%-20s   %u", "bDescriptorType", desc->bDescriptorType);
   rpt_vstring(d1, "%-20s   %2x.%02x  (0x%04x)", "bcdHID",
                   desc->bcdHID>>8, desc->bcdHID & 0x0f, desc->bcdHID);
   rpt_vstring(d1, "%-20s   %u", "bCountryCode", desc->bCountryCode);
   rpt_vstring(d1, "%-20s   %u", "bNumDescriptors", desc->bNumDescriptors);

   rpt_vstring(d1, "first bDescriptorType is at %p", &desc->bClassDescriptorType);
   int ndx = 0;
   for (;ndx < desc->bNumDescriptors; ndx++) {
      assert(sizeof(HID_Class_Descriptor) == 3);
      int offset = ndx * sizeof(HID_Class_Descriptor);
      HID_Class_Descriptor * cur =  (HID_Class_Descriptor *) (&desc->bClassDescriptorType + offset);
      rpt_vstring(d1, "cur = %p", cur);
      rpt_vstring(d1, "%-20s   %u  %s", "bDescriptorType",
                      cur->bDescriptorType, descriptor_title(cur->bDescriptorType));
      uint16_t descriptor_len = cur->wDescriptorLength;    // assumes we're on little endian system
      // uint16_t rpt_len = buf[7+3*i] | (buf[8+3*i] <<
      rpt_vstring(d1, "%-20s   %u", "wDescriptorLength", descriptor_len);

      switch(cur->bDescriptorType) {
      case LIBUSB_DT_REPORT:
      {
         rpt_vstring(d1, "Reading report descriptor of type LIBUSB_DT_REPORT from device...");

         Byte dbuf[8192];

         if (dh == NULL) {
            printf("(%s) device handle is NULL, Cannot get report descriptor\n", __func__);
         }
         else {
            int bytes_read = 0;
            bool ok = get_raw_report_descriptor(
                    dh,
                    bInterfaceNumber,
                    descriptor_len,              // report length
                    dbuf,
                    sizeof(dbuf),
                    &bytes_read);
            if (!ok)
               printf("(%s) get_raw_report_descriptor() returned %s\n", __func__, bool_repr(ok));
            if (ok) {
               puts("");
               rpt_vstring(d1, "Displaying report descriptor in HID external form:");
               Hid_Report_Descriptor_Item * item_list = tokenize_hid_report_descriptor(dbuf, bytes_read);
               report_hid_report_item_list(item_list,d2);
               Parsed_Hid_Descriptor * phd =  parse_report_desc_from_item_list(item_list);
               if (phd) {
                  puts("");
                  rpt_vstring(d1, "Parsed report descriptor:");
                  report_parsed_hid_descriptor(phd, d2);
               }
               free_hid_report_item_list(item_list);
            }
         }
         break;
      }

      case LIBUSB_DT_STRING:
         printf("(%s) Unimplemented: String report descriptor\n", __func__);
         break;

      default:
         printf("(%s) Descriptor. Type= 0x%02x\n", __func__, cur->bDescriptorType);
         break;
      }
   }
}


//
// Module initialization
//

void init_libusb_reports() {
   devid_ensure_initialized();
}

