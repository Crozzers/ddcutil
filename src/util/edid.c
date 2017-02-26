/* edid.c
 *
 * Functions for processing the EDID data structure, irrespective of how
 * the bytes of the EDID are obtained.
 *
 * This should be the only source module that understands the internal
 * structure of the EDID.
 *
 * <copyright>
 * Copyright (C) 2014-2017 Sanford Rockowitz <rockowitz@minsoft.com>
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "util/report_util.h"
#include "util/string_util.h"

#include <util/edid.h>


// Direct writes to stdout/stderr: NO


static inline bool all_bytes_zero(Byte * bytes, int len) {
   for (int ndx = 0; ndx < len; ndx++) {
      if (bytes[ndx])
         return false;
   }
   return true;
}


/* Calculates checksum for a 128 byte EDID
 *
 * Note that the checksum byte (byte 127) is itself
 * included in the checksum calculation.
 */
Byte edid_checksum(Byte * edid) {
   Byte checksum = 0;
   int ndx = 0;
   for (ndx = 0; ndx < 128; ndx++) {
      checksum += edid[ndx];
   }
   return checksum;
}


/* Unpacks the 2 byte manufacturer id field from the EDID into a 3 character
 * string.
 *
 * Arguments:
 *    mfg_id_bytes  address of first byte
 *    result        address of buffer in which to return result
 *    bufsize       buffer size; must be >= 4
 *
 * Returns:
 *    nothing
 */
void parse_mfg_id_in_buffer(Byte * mfg_id_bytes, char * result, int bufsize) {
      assert(bufsize >= 4);
      result[0] = (mfg_id_bytes[0] >> 2) & 0x1f;
      result[1] = ((mfg_id_bytes[0] & 0x03) << 3) | ((mfg_id_bytes[1] >> 5) & 0x07);
      result[2] = mfg_id_bytes[1] & 0x1f;
      // printf("result[0] = 0x%02x\n", result[0]);
      // printf("result[1] = 0x%02x\n", result[1]);
      // printf("result[2] = 0x%02x\n", result[2]);
      result[0] += 64;
      result[1] += 64;
      result[2] += 64;
      result[3] = 0;        // terminating null
}


// Extracts the 3 character manufacturer id from an EDID byte array.
// The id is returned with a trailing null in a buffer provided by the caller.
void get_edid_mfg_id_in_buffer(Byte* edidbytes, char * result, int bufsize) {
   parse_mfg_id_in_buffer(&edidbytes[8], result, bufsize);
}


#ifdef OLD

// Extracts the 3 character manufacturer id from an EDID byte array.
//
// Note it is the caller's responsibility to free the buffer returned.

char * get_edid_mfg_id(Byte * edidbytes) {
   char * result = call_malloc(4, "get_mfg_id");

   get_edid_mfg_id_in_buffer(edidbytes, result);
   return result;
}

#endif


#define EDID_DESCRIPTORS_BLOCKS_START 54
#define EDID_DESCRIPTOR_BLOCK_SIZE    18
#define EDID_DESCRIPTOR_DATA_SIZE     13
#define EDID_DESCRIPTOR_BLOCK_CT       4


#ifdef UNUSED
char * get_edid_descriptor_string(Byte * edidbytes, Byte tag) {
   assert( tag==0xff || tag==0xfe || tag==0xfc);     // valid string tags
   bool debug = true;

   static char stringbuf[EDID_DESCRIPTOR_DATA_SIZE+1];   // +1 for terminating null
   stringbuf[0] = '\0';

   // 4 descriptor blocks beginning at offset 54.  Each block is 18 bytes.
   // In each block, bytes 0-3 indicates the contents.
   int descriptor_ndx = 0;
   for (descriptor_ndx = 0; descriptor_ndx < EDID_DESCRIPTOR_BLOCK_CT; descriptor_ndx++) {
      Byte * descriptor = edidbytes +
                          EDID_DESCRIPTORS_BLOCKS_START +
                          descriptor_ndx * EDID_DESCRIPTOR_BLOCK_SIZE;
      if (debug) {
         DBGMSG("full descriptor: %s",    hexstring(descriptor, EDID_DESCRIPTOR_BLOCK_SIZE));
      }
      // test if a string descriptor
      if ( descriptor[0] == 0x00 &&       // 0x00 if not a timing descriptor
           descriptor[1] == 0x00 &&       // 0x00 if not a timing descriptor
           descriptor[2] == 0x00 &&       // &&       // 0x00 for all descriptors
           descriptor[4] == 0x00
          // (descriptor[3] == 0xff || descriptor[3] == 0xfc || descriptor[3] == 0xfe)  // 0xff: serial number, 0xfc: model name
         )
      {
         // char * nameslot = (descriptor[3] == 0xff) ? snbuf : namebuf;
         Byte * textstart = descriptor+5;
         // DBGMSF(debug, "String in descriptor: %s", hexstring(textstart, 14));
         int    textlen = 0;
         while (*(textstart+textlen) != 0x0a && textlen < 14) {
            // DBGMSG("textlen=%d, char=0x%02x", textlen, *(textstart+textlen));
            textlen++;
         }
         memcpy(stringbuf, textstart, textlen);
         stringbuf[textlen] = '\0';
      }
   }

   DBGMSF(debug, "tag=0x%02x, returning: %s", tag, stringbuf);
   return stringbuf;
}
#endif


/* Extracts the non-timing descriptors from an EDID, i.e.
 * ASCII model name, serial number, and other descriptor.
 * The extracted values are returned as null-terminated strings.
 *
 * Note that the maximum length of these strings is 13 bytes.
 *
 * Arguments:
 *   edidbytes        pointer to 128 byte EDID
 *   namebuf          pointer to buffer where model name will be returned.
 *   namebuf_len      size of namebuf, must be >= 14
 *   snbuf            pointer to buffere where serial number will be returned
 *   snbuf_len        size of snbuf, must be >= 14
 *   otherbuf         pointer to buffer where addl descriptor will be returned
 *   otherbuf_len     size of otherbuf, must be >= 14
 *
 * Returns: nothing
 *
 * Buffers will be set to "Unspecified" for descriptors that are not found.
 */

// Use Buffer instead of pointers and lengths?
// or Buffer for edidbytes, and just return pointers to newly allocated memory for found strings

static void get_edid_descriptor_strings(
        Byte* edidbytes,
        char* namebuf,
        int   namebuf_len,
        char* snbuf,
        int   snbuf_len,
        char* otherbuf,
        int   otherbuf_len)
{
   bool debug = false;
   // bool edid_ok = true;
   assert(namebuf_len >= 14 && snbuf_len >= 14 && otherbuf_len >= 14);
   strcpy(namebuf,  "Unspecified");
   strcpy(snbuf,    "Unspecified");
   strcpy(otherbuf, "Unspecified");

   int fields_found = 0;

   // 4 descriptor blocks beginning at offset 54.  Each block is 18 bytes.
   // In each block, bytes 0-3 indicates the contents.
   int descriptor_ndx = 0;
   for (descriptor_ndx = 0; descriptor_ndx < EDID_DESCRIPTOR_BLOCK_CT; descriptor_ndx++) {
      Byte * descriptor = edidbytes +
                          EDID_DESCRIPTORS_BLOCKS_START +
                          descriptor_ndx * EDID_DESCRIPTOR_BLOCK_SIZE;
      if (debug)
         printf("(%s) full descriptor: %s\n",  __func__,
                hexstring(descriptor, EDID_DESCRIPTOR_BLOCK_SIZE));

      // test if a string descriptor
      if ( descriptor[0] == 0x00 &&       // 0x00 if not a timing descriptor
           descriptor[1] == 0x00 &&       // 0x00 if not a timing descriptor
           descriptor[2] == 0x00 &&       // 0x00 for all descriptors
           descriptor[4] == 0x00
         )
      {
         char * nameslot = NULL;
         switch(descriptor[3]) {
         case 0xff:   nameslot = snbuf;     break;      // monitor serial number
         case 0xfe:   nameslot = otherbuf;  break;      // arbitrary ASCII string
         case 0xfc:   nameslot = namebuf;   break;      // monitor name
         }

         if (nameslot) {
            Byte * textstart = descriptor+5;
            // DBGMSF(debug, "String in descriptor: %s", hexstring(textstart, 14));
            int    textlen = 0;
            while (*(textstart+textlen) != 0x0a && textlen < 14) {
               // DBGMSG("textlen=%d, char=0x%02x", textlen, *(textstart+textlen));
               textlen++;
            }
            memcpy(nameslot, textstart, textlen);
            nameslot[textlen] = '\0';
            if (debug)
               printf("(%s) name = %s\n", __func__, nameslot);

         fields_found++;
         }
      }
   }

// bye:
   return;
}


Parsed_Edid * create_parsed_edid(Byte* edidbytes) {
   assert(edidbytes);
   bool debug = true;
   bool ok = true;
   Parsed_Edid* parsed_edid = NULL;

   const Byte edid_header_tag[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
   if (memcmp(edidbytes, edid_header_tag, 8) != 0) {
      char * hs = hexstring(edidbytes,8);
      if (debug)
         printf("(%s) Invalid initial EDID bytes: %s\n", __func__, hs);
      free(hs);
      goto bye;
   }

   if (edid_checksum(edidbytes) != 0x00) {
      if (debug)
         printf("(%s) Invalid EDID checksum: 0x%02x\n", __func__, edid_checksum(edidbytes));
      goto bye;
   }

   parsed_edid = calloc(1,sizeof(Parsed_Edid));
   assert(sizeof(parsed_edid->bytes) == 128);
   memcpy(parsed_edid->marker, EDID_MARKER_NAME, 4);
   memcpy(parsed_edid->bytes,  edidbytes, 128);

   get_edid_mfg_id_in_buffer(
           edidbytes,
           parsed_edid->mfg_id,
           sizeof(parsed_edid->mfg_id) );

   parsed_edid->model_hex = edidbytes[0x0b] << 8 | edidbytes[0x0a];

   parsed_edid->serial_binary = edidbytes[0x0c]       |
                                edidbytes[0x0d] <<  8 |
                                edidbytes[0x0e] << 16 |
                                edidbytes[0x0f] << 24;

   get_edid_descriptor_strings(
           edidbytes,
           parsed_edid->model_name,   sizeof(parsed_edid->model_name),
           parsed_edid->serial_ascii, sizeof(parsed_edid->serial_ascii),
           parsed_edid->extra_descriptor_string, sizeof(parsed_edid->extra_descriptor_string)
           );

   parsed_edid->year = edidbytes[17] + 1990;
   parsed_edid->is_model_year = edidbytes[16] == 0xff;
   parsed_edid->edid_version_major = edidbytes[18];
#ifdef UNNEEDED
   if (parsed_edid->edid_version_major != 1 && parsed_edid->edid_version_major != 2) {
      DBGMSF(debug, "Invalid EDID major version number: %d", parsed_edid->edid_version_major);
      ok = false;
   }
#endif
   parsed_edid->edid_version_minor = edidbytes[19];

   parsed_edid->rx = edidbytes[0x1b] << 2 | ( (edidbytes[0x19]&0b11000000)>>6 );
   parsed_edid->ry = edidbytes[0x1c] << 2 | ( (edidbytes[0x19]&0b00110000)>>4 );
   parsed_edid->gx = edidbytes[0x1d] << 2 | ( (edidbytes[0x19]&0b00001100)>>2 );
   parsed_edid->gy = edidbytes[0x1e] << 2 | ( (edidbytes[0x19]&0b00000011)>>0 );
   parsed_edid->bx = edidbytes[0x1f] << 2 | ( (edidbytes[0x1a]&0b11000000)>>6 );
   parsed_edid->by = edidbytes[0x20] << 2 | ( (edidbytes[0x1a]&0b00110000)>>4 );
   parsed_edid->wx = edidbytes[0x21] << 2 | ( (edidbytes[0x1a]&0b00001100)>>2 );
// parsed_edid->wy = edidbytes[0x22] << 2 | ( (edidbytes[0x1a]&0b00000011)>>0 );
// low order digits wrong, try another way
   parsed_edid->wy = edidbytes[0x22] * 4 + ((edidbytes[0x1a]&0b00000011)>>0);

   parsed_edid->video_input_definition = edidbytes[0x14];
   // printf("(%s) video_input_parms_bitmap = 0x%02x\n", __func__, video_input_parms_bitmap);
   // parsed_edid->is_digital_input = (parsed_edid->video_input_definition & 0x80) ? true : false;
   parsed_edid->extension_flag = edidbytes[0x7e];

   if (!ok) {
      free(parsed_edid);
      parsed_edid = NULL;
   }

bye:
   return parsed_edid;
}


void free_parsed_edid(Parsed_Edid * parsed_edid) {
   assert( parsed_edid );
   assert( memcmp(parsed_edid->marker, EDID_MARKER_NAME, 4)==0 );
   parsed_edid->marker[3] = 'x';
   free(parsed_edid);
}



/* Writes EDID summary to the current report output destination.
 * (normally stdout, but may be changed by rpt_push_output_dest())
 *
 * Arguments:
 *    edid       pointer to parsed edid struct
 *    verbose    show additional detail
 *    show_raw   include hex dump of EDID
 *    depth      logical indentation depth
 */
void report_parsed_edid_base(Parsed_Edid * edid, bool verbose, bool show_raw, int depth) {
   int d1 = depth+1;
   // verbose = true;
   if (edid) {
      rpt_vstring(depth,"EDID synopsis:");

      rpt_vstring(d1,"Mfg id:           %s",          edid->mfg_id);
      rpt_vstring(d1,"Model:            %s",          edid->model_name);
      rpt_vstring(d1,"Serial number:    %s",          edid->serial_ascii);
      char * title = (edid->is_model_year) ? "Model year" : "Manufacture year";
      rpt_vstring(d1,"%-16s: %d", title, edid->year);
      rpt_vstring(d1,"EDID version:     %d.%d", edid->edid_version_major, edid->edid_version_minor);

      if (verbose) {
      rpt_vstring(d1,"Product code:     0x%04x (%u)",      edid->model_hex, edid->model_hex);
      // useless, binary serial number is typically 0x00000000 or 0x01010101
      // rpt_vstring(d1,"Binary sn:        %u (0x%08x)", edid->serial_binary, edid->serial_binary);
      rpt_vstring(d1,"Extra descriptor: %s",          edid->extra_descriptor_string);
      char explbuf[100];
      explbuf[0] = '\0';
      if (edid->video_input_definition & 0x80) {
         strcpy(explbuf, "Digital Input");
         if (edid->edid_version_major == 1 && edid->edid_version_minor >= 4) {
            switch (edid->video_input_definition & 0x0f) {
            case 0x00:
               strcat(explbuf, "(Digital interface not defined)");
               break;
            case 0x01:
               strcat(explbuf, "(DVI)");
               break;
            case 0x02:
               strcat(explbuf, "(HDMI-a)");
               break;
            case 0x03:
               strcat(explbuf, "(HDMI-b");
               break;
            case 0x04:
               strcat(explbuf, "(MDDI)");
               break;
            case 0x05:
               strcat(explbuf, "(DisplayPort)");
               break;
            default:
               strcat(explbuf, "(Invalid DVI standard)");
            }
         }
      }
      else {
         strcpy(explbuf, "Analog Input");
      }

      rpt_vstring(d1,"Video input definition: 0x%02x - %s", edid->video_input_definition, explbuf);
   // rpt_vstring(d1,"Video input:      %s",          (edid->is_digital_input) ? "Digital" : "Analog");
      rpt_vstring(d1,"White x,y:        %.3f, %.3f",  edid->wx/1024.0, edid->wy/1024.0);
      rpt_vstring(d1,"Red   x,y:        %.3f, %.3f",  edid->rx/1024.0, edid->ry/1024.0);
      rpt_vstring(d1,"Green x,y:        %.3f, %.3f",  edid->gx/1024.0, edid->gy/1024.0);
      rpt_vstring(d1,"Blue  x,y:        %.3f, %.3f",  edid->bx/1024.0, edid->by/1024.0);
      // restrict to EDID version >= 1.3?
      rpt_vstring(d1,"Extension blocks: %u",    edid->extension_flag);
      }

      if (verbose) {
         if (edid->edid_source)
            rpt_vstring(depth,"EDID source: %s",        edid->edid_source);
      }
      if (show_raw) {
         rpt_vstring(depth,"EDID hex dump:");
         rpt_hex_dump(edid->bytes, 128, d1);
      }

   }
   else {
      // if (verbose)
         rpt_vstring(d1,"No EDID");
   }
}


/* Writes EDID summary to the current report output destination.
 * (normally stdout, but may be changed by rpt_push_output_dest())
 *
 * Arguments:
 *    edid       pointer to parsed edid struct
 *    verbose    include hex dump of EDID
 *    depth      logical indentation depth
 */
void report_parsed_edid(Parsed_Edid * edid, bool verbose, int depth) {
   report_parsed_edid_base(edid, verbose, verbose, depth);
}
