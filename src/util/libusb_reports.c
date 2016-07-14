/* libusb_reports.c
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
 */


// Adapted from usbplay2 file libusb_util.c

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <wchar.h>


#include "util/string_util.h"
#include "util/report_util.h"
// #include "util/pci_id_util.h"
#include "util/device_id_util.h"

// #include "names.h"
#include "libusb_reports.h"


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
      VN2( LIBUSB_CLASS_PER_INTERFACE,       "Per interface"),   // 0
      VN2( LIBUSB_CLASS_AUDIO,               "Audio"),           // 1
      VN2( LIBUSB_CLASS_COMM,                "Communications"),  // 2
      VN2( LIBUSB_CLASS_HID,                 "Human Interface Device"),  // 3
      VN2( LIBUSB_CLASS_PHYSICAL,            "Physical device"),  //5
      VN2( LIBUSB_CLASS_PRINTER,             "Printer"), // 7
      VN2( LIBUSB_CLASS_IMAGE,               "Image"),  // 6
      VN2( LIBUSB_CLASS_MASS_STORAGE,        "Mass storage"),  // 8
      VN2( LIBUSB_CLASS_HUB,                 "Hub"),   // 9
      VN2( LIBUSB_CLASS_DATA,                "Data"),     // 10
      VN2( LIBUSB_CLASS_SMART_CARD,          "Smart card"),     // 0x0b
      VN2( LIBUSB_CLASS_CONTENT_SECURITY,    "Content security"), // 0x0d
      VN2( LIBUSB_CLASS_VIDEO,               "Video"),            // 0x0e
      VN2( LIBUSB_CLASS_PERSONAL_HEALTHCARE, "Personal healthcare"),  // 0x0f
      VN2( LIBUSB_CLASS_DIAGNOSTIC_DEVICE,   "Diagnostic device"),    // 0xdc
      VN2( LIBUSB_CLASS_WIRELESS,            "Wireless"),             // 0xe0
      VN2( LIBUSB_CLASS_APPLICATION,         "Application"),          // 0xfe
      VN2( LIBUSB_CLASS_VENDOR_SPEC,         "Vendor specific"),       //  0xff
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
// Report functions for lubusb data structures
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


char * names_reporttag(unsigned int btag) {
   // return "Dummy tag string";
   return devid_hid_descriptor_item_type(btag);
}


// char * devid_usage_code_page_name(ushort usage_page_code);
// char * devid_usage_code_id_name(ushort usage_page_code, ushort usage_simple_id);
// huts: HID Usage Table  ??
char * names_huts(unsigned int data) {
   return "Dummy huts";
}

// hutus:  HID Usage Table Usage ?
char * names_hutus(unsigned int val) {
   return "Dummy hutus";
}

static void dump_unit(unsigned int data, unsigned int len)
{
   char *systems[5] = { "None", "SI Linear", "SI Rotation",
         "English Linear", "English Rotation" };

   char *units[5][8] = {
      { "None", "None", "None", "None", "None",
            "None", "None", "None" },
      { "None", "Centimeter", "Gram", "Seconds", "Kelvin",
            "Ampere", "Candela", "None" },
      { "None", "Radians",    "Gram", "Seconds", "Kelvin",
            "Ampere", "Candela", "None" },
      { "None", "Inch",       "Slug", "Seconds", "Fahrenheit",
            "Ampere", "Candela", "None" },
      { "None", "Degrees",    "Slug", "Seconds", "Fahrenheit",
            "Ampere", "Candela", "None" },
   };

   unsigned int i;
   unsigned int sys;
   int earlier_unit = 0;

   /* First nibble tells us which system we're in. */
   sys = data & 0xf;
   data >>= 4;

   if (sys > 4) {
      if (sys == 0xf)
         printf("System: Vendor defined, Unit: (unknown)\n");
      else
         printf("System: Reserved, Unit: (unknown)\n");
      return;
   } else {
      printf("System: %s, Unit: ", systems[sys]);
   }
   for (i = 1 ; i < len * 2 ; i++) {
      char nibble = data & 0xf;
      data >>= 4;
      if (nibble != 0) {
         if (earlier_unit++ > 0)
            printf("*");
         printf("%s", units[sys][i]);
         if (nibble != 1) {
            /* This is a _signed_ nibble(!) */

            int val = nibble & 0x7;
            if (nibble & 0x08)
               val = -((0x7 & ~val) + 1);
            printf("^%d", val);
         }
      }
   }
   if (earlier_unit == 0)
      printf("(None)");
   printf("\n");
}


 void dump_report_desc(unsigned char *b, int l)
{
   unsigned int j, bsize, btag, btype, data = 0xffff, hut = 0xffff;
   int i;
   char *types[4] = { "Main", "Global", "Local", "reserved" };
   char indent[] = "                            ";

   printf("          Report Descriptor: (length is %d)\n", l);
   for (i = 0; i < l; ) {
      bsize = b[i] & 0x03;     // first 2 bits are size indicator
      if (bsize == 3)          // values are indicators, not the size:
         bsize = 4;            //  0,1,2,4
      btype = b[i] & (0x03 << 2);    // next to bits are type
      btag = b[i] & ~0x03; /* 2 LSB bits encode length */

      printf("            Item(%-6s): %s, data=", types[btype>>2],
            names_reporttag(btag));                                       // ok
      // printf("            Item(%-6s): 0x%08x, data=",
      //        types[btype>>2],
      //        btag);
      if (bsize > 0) {
         printf(" [ ");
         data = 0;
         for (j = 0; j < bsize; j++) {
            printf("0x%02x ", b[i+1+j]);
            data += (b[i+1+j] << (8*j));
         }
         printf("] %d", data);
      } else
         printf("none");
      printf("\n");
      switch (btag) {
      case 0x04: /* Usage Page */
         // printf("%s0x%02x ", indent, data);                                //  A
         // hack
           switch(data) {
           case 0xffa0:
              printf("Fixup: data = 0xffa0 -> 0x80\n");
              data = 0x80;
              break;
           case 0xffa1:
              data = 0x81;
              break;
           }
         printf("%s%s\n", indent,
               devid_usage_code_page_name(data));
               // names_huts(data));

         hut = data;

         break;

      case 0x08: /* Usage */
      case 0x18: /* Usage Minimum */
      case 0x28: /* Usage Maximum */
      {
         // char * name = names_hutus((hut<<16) + data);
         char * name = devid_usage_code_id_name(hut,data);
         char buf[16];
         if (!name && btag == 0x08) {
            sprintf(buf, "EDID %d", data);
            name = buf;
         }
         printf("%s%s\n", indent, name);

         // printf("%s%s\n", indent,
         //        names_hutus((hut << 16) + data));                                 // B
         // printf("%s0x%08x\n", indent,
         //        (hut << 16) + data);
      }
         break;

      case 0x54: /* Unit Exponent */
         printf("%sUnit Exponent: %i\n", indent,
                (signed char)data);
         break;

      case 0x64: /* Unit */
         printf("%s", indent);
         dump_unit(data, bsize);
         break;

      case 0xa0: /* Collection */
         printf("%s", indent);
         switch (data) {
         case 0x00:
            printf("Physical\n");
            break;

         case 0x01:
            printf("Application\n");
            break;

         case 0x02:
            printf("Logical\n");
            break;

         case 0x03:
            printf("Report\n");
            break;

         case 0x04:
            printf("Named Array\n");
            break;

         case 0x05:
            printf("Usage Switch\n");
            break;

         case 0x06:
            printf("Usage Modifier\n");
            break;

         default:
            if (data & 0x80)
               printf("Vendor defined\n");
            else
               printf("Reserved for future use.\n");
         }
         break;
      case 0x80: /* Input */
      case 0x90: /* Output */
      case 0xb0: /* Feature */
         printf("%s%s %s %s %s %s\n%s%s %s %s %s\n",
                indent,
                data & 0x01  ? "Constant"   : "Data",
                data & 0x02  ? "Variable"   : "Array",
                data & 0x04  ? "Relative"   : "Absolute",
                data & 0x08  ? "Wrap"       : "No_Wrap",
                data & 0x10  ? "Non_Linear" : "Linear",
                indent,
                data & 0x20  ? "No_Preferred_State" : "Preferred_State",
                data & 0x40  ? "Null_State"     : "No_Null_Position",
                data & 0x80  ? "Volatile"       : "Non_Volatile",
                data & 0x100 ? "Buffered Bytes" : "Bitfield");
         break;
      }
      i += 1 + bsize;
   }
}


 bool get_raw_report_descriptor(
         struct libusb_device_handle * dh,
         uint8_t                       bInterfaceNumber,
         uint16_t                      rptlen,        // report length
         Byte *                        dbuf,
         int                           dbufsz)
{
    bool ok = false;
    assert(dh);
#define CTRL_RETRIES  2
#define CTRL_TIMEOUT (5*1000) /* milliseconds */

    int bytes_read;

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
          dump_report_desc(dbuf, bytes_read);
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


void report_interface_descriptor(
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

   /** Index of string descriptor describing this interface */
   // uint8_t  iInterface;
   // rpt_int("iInterface", "string descriptor index", inter->iInterface, d1);
   rpt_vstring(d1, "%-20s %d  %s",
                   "iInterface",
                   inter->iInterface,
                   "(string descriptor index)"
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
      hex_dump(inter->extra, inter->extra_length);

      if (inter->bInterfaceClass == 3) { // replace with constant
         const Byte * cur_extra = inter->extra;
         int remaining_length = inter->extra_length;
         while (remaining_length > 0) {
            HID_Descriptor * cur_hid_desc = (HID_Descriptor *) cur_extra;
            assert(cur_hid_desc->bLength <= remaining_length);
            report_hid_descriptor(dh, inter->bInterfaceNumber, cur_hid_desc, d1);

            cur_extra += cur_hid_desc->bLength;
            remaining_length -= cur_hid_desc->bLength;



#ifdef HUH
            int i, n;
            int len;
            unsigned char * buf = inter->extra;
            unsigned char dbuf[8192];
            libusb_device_handle * dev = dh;
            struct libusb_interface_descriptor *interface = inter;

#define CTRL_RETRIES  2
#define CTRL_TIMEOUT (5*1000) /* milliseconds */


            for (i = 0; i < buf[5]; i++) {
               /* we are just interested in report descriptors*/
               if (buf[6+3*i] != LIBUSB_DT_REPORT)
                  continue;
               len = buf[7+3*i] | (buf[8+3*i] << 8);
               if (len > (unsigned int)sizeof(dbuf)) {
                  printf("report descriptor too long\n");
                  continue;
               }
               if (libusb_claim_interface(dev, interface->bInterfaceNumber) == 0) {
                  int retries = 4;
                  n = 0;
                  while (n < len && retries--)
                     n = usb_control_msg(dev,
                         LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD
                           | LIBUSB_RECIPIENT_INTERFACE,
                         LIBUSB_REQUEST_GET_DESCRIPTOR,
                         (LIBUSB_DT_REPORT << 8),
                         interface->bInterfaceNumber,
                         dbuf, len,
                         CTRL_TIMEOUT);

                  if (n > 0) {
                     if (n < len)
                        printf("          Warning: incomplete report descriptor\n");
                     dump_report_desc(dbuf, n);
                  }
                  libusb_release_interface(dev, interface->bInterfaceNumber);
               } else {
                  /* recent Linuxes require claim() for RECIP_INTERFACE,
                   * so "rmmod hid" will often make these available.
                   */
                  printf("         Report Descriptors: \n"
                         "           ** UNAVAILABLE **\n");
               }
            }
#endif
         }
      }

   }
}


void report_interface(
      const struct libusb_interface *  interface,
      libusb_device_handle *           dh,    // may be null
      int                              depth)
{

   /** \ingroup desc
    * A collection of alternate settings for a particular USB interface.
    */

   int d1 = depth+1;

   rpt_structure_loc("libusb_interface", interface, depth);


      /** Array of interface descriptors. The length of this array is determined
       * by the num_altsetting field. */
      // const struct libusb_interface_descriptor *altsetting;

      /** The number of alternate settings that belong to this interface */
      // int num_altsetting;

   rpt_int("num_altsetting", NULL, interface->num_altsetting, d1);

   int ndx;
   for (ndx=0; ndx<interface->num_altsetting; ndx++) {
      // struct libusb_interface_descriptor * idesc;
      // idesc = &interface->altsetting[ndx];
      // report_interface_descriptor(idesc, dh, d1);
      report_interface_descriptor(&interface->altsetting[ndx], dh, d1);
   }
}


void report_config_descriptor(
        const struct libusb_config_descriptor * config,
        libusb_device_handle *                  dh,    // may be null
        int                                     depth)
{

   /** \ingroup desc
    * A structure representing the standard USB configuration descriptor. This
    * descriptor is documented in section 9.6.3 of the USB 3.0 specification.
    * All multiple-byte fields are represented in host-endian format.
    */

   int d1 = depth+1;

   rpt_structure_loc("libusb_config_descriptor", config, depth);

      /** Size of this descriptor (in bytes) */
      // uint8_t  bLength;
   rpt_int("bLength", NULL, config->bLength, d1);


      /** Descriptor type. Will have value
       * \ref libusb_descriptor_type::LIBUSB_DT_CONFIG LIBUSB_DT_CONFIG
       * in this context. */
     // uint8_t  bDescriptorType;
      // rpt_int("bDescriptorType", NULL, config->bDescriptorType, d1);
      rpt_vstring(d1, "%-20s 0x%02x  %s",
                      "bDescriptorType:",
                      config->bDescriptorType,
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
         report_interface(inter, dh, d1);
      }


      /** Extra descriptors. If libusb encounters unknown configuration
       * descriptors, it will store them here, should you wish to parse them. */
      // const unsigned char *extra;

      /** Length of the extra descriptors, in bytes. */
      // int extra_length;
      rpt_int("extra_length", "len of extra descriptors", config->extra_length, d1);

}


void report_device_descriptor(
        const struct libusb_device_descriptor * desc,
        libusb_device_handle *                  dh,    // may be null
        int                                     depth)
{
   /** \ingroup desc
    * A structure representing the standard USB device descriptor. This
    * descriptor is documented in section 9.6.1 of the USB 3.0 specification.
    * All multiple-byte fields are represented in host-endian format.
    */

   int d1 = depth+1;

   rpt_structure_loc("libusb_device_descriptor", desc, depth);

      /** Size of this descriptor (in bytes) */
      // uint8_t  bLength;
      rpt_int("bLength", NULL, desc->bLength, d1);

      /** Descriptor type. Will have value
       * \ref libusb_descriptor_type::LIBUSB_DT_DEVICE LIBUSB_DT_DEVICE in this
       * context. */
      // uint8_t  bDescriptorType;
      // rpt_int("bDecriptorType", NULL, desc->bDescriptorType, d1);
      rpt_vstring(d1, "%-20s 0x%02x  %s",
                      "bDescriptorType:",
                      desc->bDescriptorType,
                      descriptor_title(desc->bDescriptorType)
                 );


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
      // uint8_t  bDeviceClass;
      // rpt_int("bDeviceClass", NULL, desc->bDeviceClass, d1);
      // rpt_vstring(d1, "bDeviceClass:       0x%02x (%d)", desc->bDeviceClass, desc->bDeviceClass);
      rpt_vstring(d1, "%-20s %u  (0x%02x)  %s",
                      "bDeviceClass:",
                      desc->bDeviceClass,
                      desc->bDeviceClass,
                      class_code_title(desc->bDeviceClass) );

      /** USB-IF subclass code for the device, qualified by the bDeviceClass
       * value */
      // uint8_t  bDeviceSubClass;
      rpt_int("bDeviceSubClass", NULL, desc->bDeviceSubClass, d1);
      rpt_vstring(d1, "bDeviceSubClass:       0x%02x (%d)", desc->bDeviceSubClass, desc->bDeviceSubClass);

      /** USB-IF protocol code for the device, qualified by the bDeviceClass and
       * bDeviceSubClass values */
      // uint8_t  bDeviceProtocol;
      rpt_int("bDeviceProtocol", NULL, desc->bDeviceProtocol, d1);

      /** Maximum packet size for endpoint 0 */
      // uint8_t  bMaxPacketSize0;
      rpt_int("bMaxPacketSize0", "max size for endpoint 0", desc->bMaxPacketSize0, d1);

      Pci_Usb_Id_Names usb_id_names =
            devid_get_usb_names(
                      desc->idVendor,
                      desc->idProduct,
                      0,
                      2);

      /** USB-IF vendor ID */
      // uint16_t idVendor;
      rpt_vstring(d1, "idVendor: 0x%04x  %s", desc->idVendor, usb_id_names.vendor_name);

      // Pci_Id_Vendor * pvendor_id_info =  usb_id_find_vendor(desc->idVendor);
      // printf("(report_device_descriptor) usb_id_find_vendor() returned: %p\n", pvendor_id_info);
      // if (pvendor_id_info)
      //    printf("--> %s\n", pvendor_id_info->vendor_name);
      // free(pvendor_id_info);

      /** USB-IF product ID */
      // uint16_t idProduct;
      rpt_vstring(d1, "idProduct: 0x%04x  %s", desc->idProduct, usb_id_names.device_name);



      /** Device release number in binary-coded decimal */
      // uint16_t bcdDevice;
      bcdHi  = desc->bcdDevice >> 8;
      bcdLo  = desc->bcdDevice & 0x0f;
      rpt_vstring(d1, "bcdDevice (device release number): %2x.%02x", bcdHi, bcdLo);


      /** Index of string descriptor describing manufacturer */
      // uint8_t  iManufacturer;
      rpt_int("iManufacturer", "mfg string descriptor index", desc->iManufacturer, d1);
      char * mfg_name = "";
      if (dh)
         mfg_name = lookup_libusb_string(dh, desc->iManufacturer);
      rpt_vstring(d1, "%-20s %d  %s", "iManufacturer:", desc->iManufacturer, mfg_name);



      /** Index of string descriptor describing product */
      // uint8_t  iProduct;
      rpt_int("iProduct", "mfg string descriptor index", desc->iProduct, d1);

      /** Index of string descriptor containing device serial number */
      // uint8_t  iSerialNumber;
      rpt_int("iSerialNumber", "index of string desc for serial num", desc->iProduct, d1);

      /** Number of possible configurations */
      // uint8_t  bNumConfigurations;
      rpt_int("bNumConfigurations", "number of possible configurations", desc->bNumConfigurations, d1);
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




void report_dev(
      libusb_device *         dev,
      libusb_device_handle *  dh,    // may be null
      bool                    show_hubs,
      int                     depth)
{
   int d1 = depth+1;
   int rc;
   // int j;

   rpt_structure_loc("libusb_device", dev, depth);
   rpt_vstring(d1, "%-20s: 0x%04x", "Bus number",     libusb_get_bus_number(dev));
   rpt_vstring(d1, "%-20s: 0x%04x", "Device address", libusb_get_device_address(dev));
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
      report_device_descriptor(&desc, NULL, d1);
      struct libusb_config_descriptor *config;
      libusb_get_config_descriptor(dev, 0, &config);  // returns a pointer
      report_config_descriptor(config, dh, d1);
      libusb_free_config_descriptor(config);
   }
   printf("\n");
}


// Identify HID interfaces that that are not keyboard or mouse



static bool possible_monitor_interface_descriptor(
      const struct libusb_interface_descriptor * inter) {
   bool result = false;
   if (inter->bInterfaceClass == LIBUSB_CLASS_HID) {
      rpt_vstring(0, "bInterfaceClass:     0x%02x (%d)", inter->bInterfaceClass,    inter->bInterfaceClass);
      rpt_vstring(0, "bInterfaceSubClass:  0x%02x (%d)", inter->bInterfaceSubClass, inter->bInterfaceSubClass);
      rpt_int("bInterfaceProtocol", NULL, inter->bInterfaceProtocol, 0);
      if (inter->bInterfaceProtocol != 1 && inter->bInterfaceProtocol != 2)
         result = true;
   }
   // printf("(%s) Returning %d\n", __func__, result);
   return result;
}

static bool possible_monitor_interface(const struct libusb_interface * interface) {
   bool result = false;
   int ndx;
   for (ndx=0; ndx<interface->num_altsetting; ndx++) {
      const struct libusb_interface_descriptor * idesc = &interface->altsetting[ndx];
      // report_interface_descriptor(idesc, d1);
      result |= possible_monitor_interface_descriptor(idesc);
   }
   // if (result)
   //    printf("(%s) Returning: %d\n", __func__, result);
   return result;
}




bool possible_monitor_config_descriptor(
      const struct libusb_config_descriptor * config)
{
    bool result = false;

    int ndx = 0;
    if (config->bNumInterfaces > 1)
       printf("(%s) Examining only interface 0\n", __func__);
    // HACK: look at only interface 0
    // for (ndx=0; ndx<config->bNumInterfaces; ndx++) {
       const struct libusb_interface *inter = &(config->interface[ndx]);
       result |= possible_monitor_interface(inter);
    // }

    // if (result)
    //    printf("(%s) Returning: %d\n", __func__, result);
    return result;
}


bool possible_monitor_dev(libusb_device * dev) {
   bool result = false;

   struct libusb_config_descriptor *config;
   libusb_get_config_descriptor(dev, 0, &config);

   result = possible_monitor_config_descriptor(config);

   libusb_free_config_descriptor(config);

   // printf("(%s) Returning %d\n:" , __func__, result);
   return result;
}


struct possible_monitor_device * new_possible_monitor_device() {
   struct possible_monitor_device * cur = calloc(1, sizeof(struct possible_monitor_device));
   return cur;
}


void report_possible_monitor_device(struct possible_monitor_device * mondev, int depth) {
   int d1 = depth+1;

   rpt_structure_loc("possible_monitor_device", mondev, depth);

   rpt_vstring(d1, "%-20s   %p", "libusb_device", mondev->libusb_device);
   rpt_vstring(d1, "%-20s   %d", "bus", mondev->bus);
   rpt_vstring(d1, "%-20s   %d", "device_address", mondev->device_address);
   rpt_vstring(d1, "%-20s   0x%04x", "vid", mondev->vid);
   rpt_vstring(d1, "%-20s   0x%04x", "pid", mondev->pid);
   rpt_vstring(d1, "%-20s   %d", "interface", mondev->interface);
   rpt_vstring(d1, "%-20s   %d", "alt_setting", mondev->alt_setting);
   rpt_vstring(d1, "%-20s   %s", "manufacturer_name", mondev->manufacturer_name);
   rpt_vstring(d1, "%-20s   %s", "product_name", mondev->product_name);
   rpt_vstring(d1, "%-20s   %s", "serial_number_ascii", mondev->serial_number);
// rpt_vstring(d1, "%-20s   %S", "serial_number_wide", mondev->serial_number_wide);
   rpt_vstring(d1, "%-20s   %p", "next", mondev->next);
}


void report_possible_monitors(struct possible_monitor_device * mondev_head, int depth) {
   rpt_title("Possible monitor devices:", depth);
   if (!mondev_head) {
      rpt_title("None", depth+1);
   }
   else {
      struct possible_monitor_device * cur = mondev_head;
      while(cur) {
         report_possible_monitor_device(cur, depth+1);
         cur = cur->next;
      }
   }
}


struct possible_monitor_device * get_possible_monitors(
       libusb_device **devs      // null terminated list
      )
{
   // struct possible_monitor_device * last_device = new_possible_monitor_device();
   // struct possible_mointor_device * head_device = last_device;

   Possible_Monitor_Device * last_device = new_possible_monitor_device();
   Possible_Monitor_Device * head_device = last_device;


   int rc;
   libusb_device *dev;

   int i = 0;
   while ((dev = devs[i++]) != NULL) {
      // report_dev(dev, NULL, false /* show hubs */, 0);
      int bus            = libusb_get_bus_number(dev);
      int device_address = libusb_get_device_address(dev);
      ushort vid = 0;
      ushort pid = 0;

      struct libusb_device_descriptor desc;
      rc = libusb_get_device_descriptor(dev, &desc);
      CHECK_LIBUSB_RC("libusb_device_descriptor", rc, LIBUSB_EXIT);
      vid = desc.idVendor;
      pid = desc.idProduct;

      struct libusb_config_descriptor * config;
      rc = libusb_get_config_descriptor(dev, 0, &config);   // returns a pointer
      CHECK_LIBUSB_RC("libusb_config_descriptor", rc, LIBUSB_EXIT);

      // Logitech receiver has subclass 0 on interface 2,
      // try ignoring all interfaces other than 0
      int inter_no = 0;
      // for (inter_no=0; inter_no<config->bNumInterfaces; inter_no++) {
         const struct libusb_interface *inter = &(config->interface[inter_no]);

         int altset_no;
         for (altset_no=0; altset_no<inter->num_altsetting; altset_no++) {
            const struct libusb_interface_descriptor * idesc  = &inter->altsetting[altset_no];

            if (idesc->bInterfaceClass == LIBUSB_CLASS_HID) {
               rpt_vstring(0, "bInterfaceClass:     0x%02x (%d)", idesc->bInterfaceClass,    idesc->bInterfaceClass);
               rpt_vstring(0, "bInterfaceSubClass:  0x%02x (%d)", idesc->bInterfaceSubClass, idesc->bInterfaceSubClass);
               rpt_int("bInterfaceProtocol", NULL, idesc->bInterfaceProtocol, 0);

               if (idesc->bInterfaceProtocol != 1 && idesc->bInterfaceProtocol != 2)
               {
                  // TO ADDRESS: WHAT IF MULTIPLE altsettings?  what if they conflict?

                  struct possible_monitor_device * new_node = new_possible_monitor_device();
                  libusb_ref_device(dev);
                  new_node->libusb_device = dev;
                  new_node->bus = bus;
                  new_node->device_address = device_address;
                  new_node->alt_setting = altset_no;
                  new_node->interface = inter_no;
                  new_node->vid = vid;
                  new_node->pid = pid;

                  struct libusb_device_handle * dh = NULL;
                  rc = libusb_open(dev, &dh);
                  if (rc < 0) {
                     REPORT_LIBUSB_ERROR("libusb_open", rc, LIBUSB_CONTINUE);
                     dh = NULL;   // belt and suspenders
                  }
                  else {
                     printf("(%s) Successfully opened\n", __func__);
                     rc = libusb_set_auto_detach_kernel_driver(dh, 1);
                  }

                  report_dev(dev, dh, false, 0);

                  if (dh) {
                     printf("Manufacturer:  %d - %s\n",
                               desc.iManufacturer,
                               lookup_libusb_string(dh, desc.iManufacturer) );
                     printf("Product:  %d - %s\n",
                               desc.iProduct,
                               lookup_libusb_string(dh, desc.iProduct) );
                     printf("Serial number:  %d - %s\n",
                                          desc.iSerialNumber,
                                          lookup_libusb_string(dh, desc.iSerialNumber) );
                     new_node->manufacturer_name = strdup(lookup_libusb_string(dh, desc.iManufacturer));
                     new_node->product_name      = strdup(lookup_libusb_string(dh, desc.iProduct));
                     new_node->serial_number     = strdup(lookup_libusb_string(dh, desc.iSerialNumber));
                  // new_node->serial_number_wide = wcsdup(lookup_libusb_string_wide(dh, desc.iSerialNumber));
                     // printf("(%s) serial_number_wide = |%S|\n", __func__, new_node->serial_number_wide);

                     // report_device_descriptor(&desc, NULL, d1);
                     // report_open_libusb_device(dh, 1);
                     libusb_close(dh);
                  }

                  last_device->next = new_node;
                  last_device = new_node;

               }
            }
         }
      // }   // interfaces

      libusb_free_config_descriptor(config);
   }

   struct possible_monitor_device * true_head;
   true_head = head_device->next;
   return true_head;
}



bool is_hub_descriptor(const struct libusb_device_descriptor * desc) {
   return (desc->bDeviceClass == 9);
}


// copied and modified from make_path() in libusb/hid.c in hidapi

char *make_path(int bus_number, int device_address, int interface_number)
{
   char str[64];
   snprintf(str, sizeof(str), "%04x:%04x:%02x",
      bus_number,
      device_address,
      interface_number);
   str[sizeof(str)-1] = '\0';

   return strdup(str);
}


char *make_path_from_libusb_device(libusb_device *dev, int interface_number)
{
   return make_path(libusb_get_bus_number(dev), libusb_get_device_address(dev), interface_number);
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
   bool debug = true;
   if (debug)
      printf("(%s) Starting. dh=%p, bInterfaceNumber=%d, desc=%p\n",
            __func__, dh, bInterfaceNumber, desc);
   int d1 = depth+1;

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

      if (cur->bDescriptorType != LIBUSB_DT_REPORT)
           continue;

      Byte dbuf[8192];

      if (dh == NULL) {
         printf("(%s) device handle is NULL, unable to get report descriptor\n", __func__);
      }
      else {
         bool ok = get_raw_report_descriptor(
                 dh,    // may be null, need to check
                 bInterfaceNumber,
                 descriptor_len,              // report length
                 dbuf,
                 sizeof(dbuf) );
         printf("(%s) get_raw_report_descriptor() returned %s\n", __func__, bool_repr(ok));
      }
   }


}


void init_names() {
   devid_ensure_initialized();
}


// From usbplay2.c

// TODO: return a data structure
void collect_possible_monitor_devs( libusb_device **devs) {
   libusb_device *dev;

   int has_detach_kernel_capability =
         libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
   printf("(%s) %s kernel detach driver capability\n",
          __func__,
          (has_detach_kernel_capability) ?
                "Has" : "Does not have");

   int i = 0;
   while ((dev = devs[i++]) != NULL) {

      unsigned short busno =  libusb_get_bus_number(dev);
      unsigned short devno =  libusb_get_device_address(dev);

      printf("(%s) Examining device. bus=0x%04x, device=0x%04x\n", __func__, busno, devno);
      bool possible = possible_monitor_dev(dev);
      if (possible) {
         printf("(%s) Found potential HID device. busno=0x%04x, devno=0x%04x\n", __func__, busno, devno);

         struct libusb_device_handle * dh = NULL;
         int rc = libusb_open(dev, &dh);
         if (rc < 0) {
            REPORT_LIBUSB_ERROR("libusb_open", rc, LIBUSB_CONTINUE);
            dh = NULL;   // belt and suspenders
         }
         else {
            printf("(%s) Successfully opened\n", __func__);
            if (has_detach_kernel_capability) {
               rc = libusb_set_auto_detach_kernel_driver(dh, 1);
               if (rc < 0) {
                  REPORT_LIBUSB_ERROR("libusb_set_auto_detach_kernel_driver", rc, LIBUSB_CONTINUE);
               }
            }
         }

         report_dev(dev, dh, false, 0);


         if (dh) {
            struct libusb_device_descriptor desc;
            // copies data into struct pointed to by desc, does not allocate:
            rc = libusb_get_device_descriptor(dev, &desc);
            if (rc < 0)
               REPORT_LIBUSB_ERROR("libusb_get_device_descriptor",  rc, LIBUSB_EXIT);

            printf("Manufacturer:  %d - %s\n",
                      desc.iManufacturer,
                      lookup_libusb_string(dh, desc.iManufacturer) );


            printf("Product:  %d - %s\n",
                      desc.iProduct,
                      lookup_libusb_string(dh, desc.iProduct) );

            printf("Serial number:  %d - %s\n",
                                 desc.iSerialNumber,
                                 lookup_libusb_string(dh, desc.iSerialNumber) );

            // report_device_descriptor(&desc, NULL, d1);
            // report_open_libusb_device(dh, 1);
            libusb_close(dh);
         }
      }
   }
}


// Report a list of libusb_devices
static void report_libusb_devices(libusb_device **devs, bool show_hubs, int depth)
{
      libusb_device *dev;

      int i = 0;
      while ((dev = devs[i++]) != NULL) {
         report_dev(dev, NULL, show_hubs, depth);
      }
}


// Probe using libusb
void probe_libusb(bool possible_monitors_only) {
   printf("(libusb) Starting\n");

   bool ok = devid_ensure_initialized();
   printf("(libusb) devid_ensure_initialized() returned: %s\n", bool_repr(ok));

   libusb_device **devs;
   libusb_context *ctx = NULL; //a libusb session
   int r;
   ssize_t cnt;   // number of devices in list

   r = libusb_init(&ctx);   // initialize a library session
   CHECK_LIBUSB_RC("libusb_init", r, LIBUSB_EXIT);
   libusb_set_debug(ctx,3);

   cnt = libusb_get_device_list(ctx, &devs);
   CHECK_LIBUSB_RC("libusb_get_device_list", (int) cnt, LIBUSB_EXIT);

   if (!possible_monitors_only)
      report_libusb_devices(devs, false /* show_hubs */, 0);

   // ignore result for now
   printf("(%s) =========== Calling collect_possible_monitor_devs() ==============\n", __func__);
   collect_possible_monitor_devs(devs);   // this is just a filter!

   libusb_free_device_list(devs, 1 /* unref the devices in the list */);

   libusb_exit(ctx);
}


